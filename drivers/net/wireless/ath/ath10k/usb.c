// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2007-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012,2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2016-2017 Erik Stromdahl <erik.stromdahl@gmail.com>
 */

#include <linux/module.h>
#include <linux/usb.h>

#include "debug.h"
#include "core.h"
#include "bmi.h"
#include "hif.h"
#include "htc.h"
#include "usb.h"

static void ath10k_usb_post_recv_transfers(struct ath10k *ar,
					   struct ath10k_usb_pipe *recv_pipe);

/* inlined helper functions */

static inline enum ath10k_htc_ep_id
eid_from_htc_hdr(struct ath10k_htc_hdr *htc_hdr)
{
	return (enum ath10k_htc_ep_id)htc_hdr->eid;
}

static inline bool is_trailer_only_msg(struct ath10k_htc_hdr *htc_hdr)
{
	return __le16_to_cpu(htc_hdr->len) == htc_hdr->trailer_len;
}

/* pipe/urb operations */
static struct ath10k_urb_context *
ath10k_usb_alloc_urb_from_pipe(struct ath10k_usb_pipe *pipe)
{
	struct ath10k_urb_context *urb_context = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pipe->ar_usb->cs_lock, flags);
	if (!list_empty(&pipe->urb_list_head)) {
		urb_context = list_first_entry(&pipe->urb_list_head,
					       struct ath10k_urb_context, link);
		list_del(&urb_context->link);
		pipe->urb_cnt--;
	}
	spin_unlock_irqrestore(&pipe->ar_usb->cs_lock, flags);

	return urb_context;
}

static void ath10k_usb_free_urb_to_pipe(struct ath10k_usb_pipe *pipe,
					struct ath10k_urb_context *urb_context)
{
	unsigned long flags;

	spin_lock_irqsave(&pipe->ar_usb->cs_lock, flags);

	pipe->urb_cnt++;
	list_add(&urb_context->link, &pipe->urb_list_head);

	spin_unlock_irqrestore(&pipe->ar_usb->cs_lock, flags);
}

static void ath10k_usb_cleanup_recv_urb(struct ath10k_urb_context *urb_context)
{
	dev_kfree_skb(urb_context->skb);
	urb_context->skb = NULL;

	ath10k_usb_free_urb_to_pipe(urb_context->pipe, urb_context);
}

static void ath10k_usb_free_pipe_resources(struct ath10k *ar,
					   struct ath10k_usb_pipe *pipe)
{
	struct ath10k_urb_context *urb_context;

	if (!pipe->ar_usb) {
		/* nothing allocated for this pipe */
		return;
	}

	ath10k_dbg(ar, ATH10K_DBG_USB,
		   "usb free resources lpipe %d hpipe 0x%x urbs %d avail %d\n",
		   pipe->logical_pipe_num, pipe->usb_pipe_handle,
		   pipe->urb_alloc, pipe->urb_cnt);

	if (pipe->urb_alloc != pipe->urb_cnt) {
		ath10k_dbg(ar, ATH10K_DBG_USB,
			   "usb urb leak lpipe %d hpipe 0x%x urbs %d avail %d\n",
			   pipe->logical_pipe_num, pipe->usb_pipe_handle,
			   pipe->urb_alloc, pipe->urb_cnt);
	}

	for (;;) {
		urb_context = ath10k_usb_alloc_urb_from_pipe(pipe);

		if (!urb_context)
			break;

		kfree(urb_context);
	}
}

static void ath10k_usb_cleanup_pipe_resources(struct ath10k *ar)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);
	int i;

	for (i = 0; i < ATH10K_USB_PIPE_MAX; i++)
		ath10k_usb_free_pipe_resources(ar, &ar_usb->pipes[i]);
}

/* hif usb rx/tx completion functions */

static void ath10k_usb_recv_complete(struct urb *urb)
{
	struct ath10k_urb_context *urb_context = urb->context;
	struct ath10k_usb_pipe *pipe = urb_context->pipe;
	struct ath10k *ar = pipe->ar_usb->ar;
	struct sk_buff *skb;
	int status = 0;

	ath10k_dbg(ar, ATH10K_DBG_USB_BULK,
		   "usb recv pipe %d stat %d len %d urb 0x%pK\n",
		   pipe->logical_pipe_num, urb->status, urb->actual_length,
		   urb);

	if (urb->status != 0) {
		status = -EIO;
		switch (urb->status) {
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/* no need to spew these errors when device
			 * removed or urb killed due to driver shutdown
			 */
			status = -ECANCELED;
			break;
		default:
			ath10k_dbg(ar, ATH10K_DBG_USB_BULK,
				   "usb recv pipe %d ep 0x%2.2x failed: %d\n",
				   pipe->logical_pipe_num,
				   pipe->ep_address, urb->status);
			break;
		}
		goto cleanup_recv_urb;
	}

	if (urb->actual_length == 0)
		goto cleanup_recv_urb;

	skb = urb_context->skb;

	/* we are going to pass it up */
	urb_context->skb = NULL;
	skb_put(skb, urb->actual_length);

	/* note: queue implements a lock */
	skb_queue_tail(&pipe->io_comp_queue, skb);
	schedule_work(&pipe->io_complete_work);

cleanup_recv_urb:
	ath10k_usb_cleanup_recv_urb(urb_context);

	if (status == 0 &&
	    pipe->urb_cnt >= pipe->urb_cnt_thresh) {
		/* our free urbs are piling up, post more transfers */
		ath10k_usb_post_recv_transfers(ar, pipe);
	}
}

static void ath10k_usb_transmit_complete(struct urb *urb)
{
	struct ath10k_urb_context *urb_context = urb->context;
	struct ath10k_usb_pipe *pipe = urb_context->pipe;
	struct ath10k *ar = pipe->ar_usb->ar;
	struct sk_buff *skb;

	if (urb->status != 0) {
		ath10k_dbg(ar, ATH10K_DBG_USB_BULK,
			   "pipe: %d, failed:%d\n",
			   pipe->logical_pipe_num, urb->status);
	}

	skb = urb_context->skb;
	urb_context->skb = NULL;
	ath10k_usb_free_urb_to_pipe(urb_context->pipe, urb_context);

	/* note: queue implements a lock */
	skb_queue_tail(&pipe->io_comp_queue, skb);
	schedule_work(&pipe->io_complete_work);
}

/* pipe operations */
static void ath10k_usb_post_recv_transfers(struct ath10k *ar,
					   struct ath10k_usb_pipe *recv_pipe)
{
	struct ath10k_urb_context *urb_context;
	struct urb *urb;
	int usb_status;

	for (;;) {
		urb_context = ath10k_usb_alloc_urb_from_pipe(recv_pipe);
		if (!urb_context)
			break;

		urb_context->skb = dev_alloc_skb(ATH10K_USB_RX_BUFFER_SIZE);
		if (!urb_context->skb)
			goto err;

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			goto err;

		usb_fill_bulk_urb(urb,
				  recv_pipe->ar_usb->udev,
				  recv_pipe->usb_pipe_handle,
				  urb_context->skb->data,
				  ATH10K_USB_RX_BUFFER_SIZE,
				  ath10k_usb_recv_complete, urb_context);

		ath10k_dbg(ar, ATH10K_DBG_USB_BULK,
			   "usb bulk recv submit %d 0x%x ep 0x%2.2x len %d buf 0x%pK\n",
			   recv_pipe->logical_pipe_num,
			   recv_pipe->usb_pipe_handle, recv_pipe->ep_address,
			   ATH10K_USB_RX_BUFFER_SIZE, urb_context->skb);

		usb_anchor_urb(urb, &recv_pipe->urb_submitted);
		usb_status = usb_submit_urb(urb, GFP_ATOMIC);

		if (usb_status) {
			ath10k_dbg(ar, ATH10K_DBG_USB_BULK,
				   "usb bulk recv failed: %d\n",
				   usb_status);
			usb_unanchor_urb(urb);
			usb_free_urb(urb);
			goto err;
		}
		usb_free_urb(urb);
	}

	return;

err:
	ath10k_usb_cleanup_recv_urb(urb_context);
}

static void ath10k_usb_flush_all(struct ath10k *ar)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);
	int i;

	for (i = 0; i < ATH10K_USB_PIPE_MAX; i++) {
		if (ar_usb->pipes[i].ar_usb) {
			usb_kill_anchored_urbs(&ar_usb->pipes[i].urb_submitted);
			cancel_work_sync(&ar_usb->pipes[i].io_complete_work);
		}
	}
}

static void ath10k_usb_start_recv_pipes(struct ath10k *ar)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);

	ar_usb->pipes[ATH10K_USB_PIPE_RX_DATA].urb_cnt_thresh = 1;

	ath10k_usb_post_recv_transfers(ar,
				       &ar_usb->pipes[ATH10K_USB_PIPE_RX_DATA]);
}

static void ath10k_usb_tx_complete(struct ath10k *ar, struct sk_buff *skb)
{
	struct ath10k_htc_hdr *htc_hdr;
	struct ath10k_htc_ep *ep;

	htc_hdr = (struct ath10k_htc_hdr *)skb->data;
	ep = &ar->htc.endpoint[htc_hdr->eid];
	ath10k_htc_notify_tx_completion(ep, skb);
	/* The TX complete handler now owns the skb... */
}

static void ath10k_usb_rx_complete(struct ath10k *ar, struct sk_buff *skb)
{
	struct ath10k_htc *htc = &ar->htc;
	struct ath10k_htc_hdr *htc_hdr;
	enum ath10k_htc_ep_id eid;
	struct ath10k_htc_ep *ep;
	u16 payload_len;
	u8 *trailer;
	int ret;

	htc_hdr = (struct ath10k_htc_hdr *)skb->data;
	eid = eid_from_htc_hdr(htc_hdr);
	ep = &ar->htc.endpoint[eid];

	if (ep->service_id == 0) {
		ath10k_warn(ar, "ep %d is not connected\n", eid);
		goto out_free_skb;
	}

	payload_len = le16_to_cpu(htc_hdr->len);
	if (!payload_len) {
		ath10k_warn(ar, "zero length frame received, firmware crashed?\n");
		goto out_free_skb;
	}

	if (payload_len < htc_hdr->trailer_len) {
		ath10k_warn(ar, "malformed frame received, firmware crashed?\n");
		goto out_free_skb;
	}

	if (htc_hdr->flags & ATH10K_HTC_FLAG_TRAILER_PRESENT) {
		trailer = skb->data + sizeof(*htc_hdr) + payload_len -
			  htc_hdr->trailer_len;

		ret = ath10k_htc_process_trailer(htc,
						 trailer,
						 htc_hdr->trailer_len,
						 eid,
						 NULL,
						 NULL);
		if (ret)
			goto out_free_skb;

		if (is_trailer_only_msg(htc_hdr))
			goto out_free_skb;

		/* strip off the trailer from the skb since it should not
		 * be passed on to upper layers
		 */
		skb_trim(skb, skb->len - htc_hdr->trailer_len);
	}

	skb_pull(skb, sizeof(*htc_hdr));
	ep->ep_ops.ep_rx_complete(ar, skb);
	/* The RX complete handler now owns the skb... */

	return;

out_free_skb:
	dev_kfree_skb(skb);
}

static void ath10k_usb_io_comp_work(struct work_struct *work)
{
	struct ath10k_usb_pipe *pipe = container_of(work,
						    struct ath10k_usb_pipe,
						    io_complete_work);
	struct ath10k *ar = pipe->ar_usb->ar;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&pipe->io_comp_queue))) {
		if (pipe->flags & ATH10K_USB_PIPE_FLAG_TX)
			ath10k_usb_tx_complete(ar, skb);
		else
			ath10k_usb_rx_complete(ar, skb);
	}
}

#define ATH10K_USB_MAX_DIAG_CMD (sizeof(struct ath10k_usb_ctrl_diag_cmd_write))
#define ATH10K_USB_MAX_DIAG_RESP (sizeof(struct ath10k_usb_ctrl_diag_resp_read))

static void ath10k_usb_destroy(struct ath10k *ar)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);

	ath10k_usb_flush_all(ar);
	ath10k_usb_cleanup_pipe_resources(ar);
	usb_set_intfdata(ar_usb->interface, NULL);

	kfree(ar_usb->diag_cmd_buffer);
	kfree(ar_usb->diag_resp_buffer);
}

static int ath10k_usb_hif_start(struct ath10k *ar)
{
	int i;
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);

	ath10k_usb_start_recv_pipes(ar);

	/* set the TX resource avail threshold for each TX pipe */
	for (i = ATH10K_USB_PIPE_TX_CTRL;
	     i <= ATH10K_USB_PIPE_TX_DATA_HP; i++) {
		ar_usb->pipes[i].urb_cnt_thresh =
		    ar_usb->pipes[i].urb_alloc / 2;
	}

	return 0;
}

static int ath10k_usb_hif_tx_sg(struct ath10k *ar, u8 pipe_id,
				struct ath10k_hif_sg_item *items, int n_items)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);
	struct ath10k_usb_pipe *pipe = &ar_usb->pipes[pipe_id];
	struct ath10k_urb_context *urb_context;
	struct sk_buff *skb;
	struct urb *urb;
	int ret, i;

	for (i = 0; i < n_items; i++) {
		urb_context = ath10k_usb_alloc_urb_from_pipe(pipe);
		if (!urb_context) {
			ret = -ENOMEM;
			goto err;
		}

		skb = items[i].transfer_context;
		urb_context->skb = skb;

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb) {
			ret = -ENOMEM;
			goto err_free_urb_to_pipe;
		}

		usb_fill_bulk_urb(urb,
				  ar_usb->udev,
				  pipe->usb_pipe_handle,
				  skb->data,
				  skb->len,
				  ath10k_usb_transmit_complete, urb_context);

		if (!(skb->len % pipe->max_packet_size)) {
			/* hit a max packet boundary on this pipe */
			urb->transfer_flags |= URB_ZERO_PACKET;
		}

		usb_anchor_urb(urb, &pipe->urb_submitted);
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret) {
			ath10k_dbg(ar, ATH10K_DBG_USB_BULK,
				   "usb bulk transmit failed: %d\n", ret);
			usb_unanchor_urb(urb);
			usb_free_urb(urb);
			ret = -EINVAL;
			goto err_free_urb_to_pipe;
		}

		usb_free_urb(urb);
	}

	return 0;

err_free_urb_to_pipe:
	ath10k_usb_free_urb_to_pipe(urb_context->pipe, urb_context);
err:
	return ret;
}

static void ath10k_usb_hif_stop(struct ath10k *ar)
{
	ath10k_usb_flush_all(ar);
}

static u16 ath10k_usb_hif_get_free_queue_number(struct ath10k *ar, u8 pipe_id)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);

	return ar_usb->pipes[pipe_id].urb_cnt;
}

static int ath10k_usb_submit_ctrl_out(struct ath10k *ar,
				      u8 req, u16 value, u16 index, void *data,
				      u32 size)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);
	u8 *buf = NULL;
	int ret;

	if (size > 0) {
		buf = kmemdup(data, size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	}

	/* note: if successful returns number of bytes transferred */
	ret = usb_control_msg(ar_usb->udev,
			      usb_sndctrlpipe(ar_usb->udev, 0),
			      req,
			      USB_DIR_OUT | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, value, index, buf,
			      size, 1000);

	if (ret < 0) {
		ath10k_warn(ar, "Failed to submit usb control message: %d\n",
			    ret);
		kfree(buf);
		return ret;
	}

	kfree(buf);

	return 0;
}

static int ath10k_usb_submit_ctrl_in(struct ath10k *ar,
				     u8 req, u16 value, u16 index, void *data,
				     u32 size)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);
	u8 *buf = NULL;
	int ret;

	if (size > 0) {
		buf = kmalloc(size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	}

	/* note: if successful returns number of bytes transferred */
	ret = usb_control_msg(ar_usb->udev,
			      usb_rcvctrlpipe(ar_usb->udev, 0),
			      req,
			      USB_DIR_IN | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, value, index, buf,
			      size, 2 * HZ);

	if (ret < 0) {
		ath10k_warn(ar, "Failed to read usb control message: %d\n",
			    ret);
		kfree(buf);
		return ret;
	}

	memcpy((u8 *)data, buf, size);

	kfree(buf);

	return 0;
}

static int ath10k_usb_ctrl_msg_exchange(struct ath10k *ar,
					u8 req_val, u8 *req_buf, u32 req_len,
					u8 resp_val, u8 *resp_buf,
					u32 *resp_len)
{
	int ret;

	/* send command */
	ret = ath10k_usb_submit_ctrl_out(ar, req_val, 0, 0,
					 req_buf, req_len);
	if (ret)
		goto err;

	/* get response */
	if (resp_buf) {
		ret = ath10k_usb_submit_ctrl_in(ar, resp_val, 0, 0,
						resp_buf, *resp_len);
		if (ret)
			goto err;
	}

	return 0;
err:
	return ret;
}

static int ath10k_usb_hif_diag_read(struct ath10k *ar, u32 address, void *buf,
				    size_t buf_len)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);
	struct ath10k_usb_ctrl_diag_cmd_read *cmd;
	u32 resp_len;
	int ret;

	if (buf_len < sizeof(struct ath10k_usb_ctrl_diag_resp_read))
		return -EINVAL;

	cmd = (struct ath10k_usb_ctrl_diag_cmd_read *)ar_usb->diag_cmd_buffer;
	memset(cmd, 0, sizeof(*cmd));
	cmd->cmd = ATH10K_USB_CTRL_DIAG_CC_READ;
	cmd->address = cpu_to_le32(address);
	resp_len = sizeof(struct ath10k_usb_ctrl_diag_resp_read);

	ret = ath10k_usb_ctrl_msg_exchange(ar,
					   ATH10K_USB_CONTROL_REQ_DIAG_CMD,
					   (u8 *)cmd,
					   sizeof(*cmd),
					   ATH10K_USB_CONTROL_REQ_DIAG_RESP,
					   ar_usb->diag_resp_buffer, &resp_len);
	if (ret)
		return ret;

	if (resp_len != sizeof(struct ath10k_usb_ctrl_diag_resp_read))
		return -EMSGSIZE;

	memcpy(buf, ar_usb->diag_resp_buffer,
	       sizeof(struct ath10k_usb_ctrl_diag_resp_read));

	return 0;
}

static int ath10k_usb_hif_diag_write(struct ath10k *ar, u32 address,
				     const void *data, int nbytes)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);
	struct ath10k_usb_ctrl_diag_cmd_write *cmd;
	int ret;

	if (nbytes != sizeof(cmd->value))
		return -EINVAL;

	cmd = (struct ath10k_usb_ctrl_diag_cmd_write *)ar_usb->diag_cmd_buffer;
	memset(cmd, 0, sizeof(*cmd));
	cmd->cmd = cpu_to_le32(ATH10K_USB_CTRL_DIAG_CC_WRITE);
	cmd->address = cpu_to_le32(address);
	memcpy(&cmd->value, data, nbytes);

	ret = ath10k_usb_ctrl_msg_exchange(ar,
					   ATH10K_USB_CONTROL_REQ_DIAG_CMD,
					   (u8 *)cmd,
					   sizeof(*cmd),
					   0, NULL, NULL);
	if (ret)
		return ret;

	return 0;
}

static int ath10k_usb_bmi_exchange_msg(struct ath10k *ar,
				       void *req, u32 req_len,
				       void *resp, u32 *resp_len)
{
	int ret;

	if (req) {
		ret = ath10k_usb_submit_ctrl_out(ar,
						 ATH10K_USB_CONTROL_REQ_SEND_BMI_CMD,
						 0, 0, req, req_len);
		if (ret) {
			ath10k_warn(ar,
				    "unable to send the bmi data to the device: %d\n",
				    ret);
			return ret;
		}
	}

	if (resp) {
		ret = ath10k_usb_submit_ctrl_in(ar,
						ATH10K_USB_CONTROL_REQ_RECV_BMI_RESP,
						0, 0, resp, *resp_len);
		if (ret) {
			ath10k_warn(ar,
				    "Unable to read the bmi data from the device: %d\n",
				    ret);
			return ret;
		}
	}

	return 0;
}

static void ath10k_usb_hif_get_default_pipe(struct ath10k *ar,
					    u8 *ul_pipe, u8 *dl_pipe)
{
	*ul_pipe = ATH10K_USB_PIPE_TX_CTRL;
	*dl_pipe = ATH10K_USB_PIPE_RX_CTRL;
}

static int ath10k_usb_hif_map_service_to_pipe(struct ath10k *ar, u16 svc_id,
					      u8 *ul_pipe, u8 *dl_pipe)
{
	switch (svc_id) {
	case ATH10K_HTC_SVC_ID_RSVD_CTRL:
	case ATH10K_HTC_SVC_ID_WMI_CONTROL:
		*ul_pipe = ATH10K_USB_PIPE_TX_CTRL;
		/* due to large control packets, shift to data pipe */
		*dl_pipe = ATH10K_USB_PIPE_RX_DATA;
		break;
	case ATH10K_HTC_SVC_ID_HTT_DATA_MSG:
		*ul_pipe = ATH10K_USB_PIPE_TX_DATA_LP;
		/* Disable rxdata2 directly, it will be enabled
		 * if FW enable rxdata2
		 */
		*dl_pipe = ATH10K_USB_PIPE_RX_DATA;
		break;
	default:
		return -EPERM;
	}

	return 0;
}

/* This op is currently only used by htc_wait_target if the HTC ready
 * message times out. It is not applicable for USB since there is nothing
 * we can do if the HTC ready message does not arrive in time.
 * TODO: Make this op non mandatory by introducing a NULL check in the
 * hif op wrapper.
 */
static void ath10k_usb_hif_send_complete_check(struct ath10k *ar,
					       u8 pipe, int force)
{
}

static int ath10k_usb_hif_power_up(struct ath10k *ar,
				   enum ath10k_firmware_mode fw_mode)
{
	return 0;
}

static void ath10k_usb_hif_power_down(struct ath10k *ar)
{
	ath10k_usb_flush_all(ar);
}

#ifdef CONFIG_PM

static int ath10k_usb_hif_suspend(struct ath10k *ar)
{
	return -EOPNOTSUPP;
}

static int ath10k_usb_hif_resume(struct ath10k *ar)
{
	return -EOPNOTSUPP;
}
#endif

static const struct ath10k_hif_ops ath10k_usb_hif_ops = {
	.tx_sg			= ath10k_usb_hif_tx_sg,
	.diag_read		= ath10k_usb_hif_diag_read,
	.diag_write		= ath10k_usb_hif_diag_write,
	.exchange_bmi_msg	= ath10k_usb_bmi_exchange_msg,
	.start			= ath10k_usb_hif_start,
	.stop			= ath10k_usb_hif_stop,
	.map_service_to_pipe	= ath10k_usb_hif_map_service_to_pipe,
	.get_default_pipe	= ath10k_usb_hif_get_default_pipe,
	.send_complete_check	= ath10k_usb_hif_send_complete_check,
	.get_free_queue_number	= ath10k_usb_hif_get_free_queue_number,
	.power_up		= ath10k_usb_hif_power_up,
	.power_down		= ath10k_usb_hif_power_down,
#ifdef CONFIG_PM
	.suspend		= ath10k_usb_hif_suspend,
	.resume			= ath10k_usb_hif_resume,
#endif
};

static u8 ath10k_usb_get_logical_pipe_num(u8 ep_address, int *urb_count)
{
	u8 pipe_num = ATH10K_USB_PIPE_INVALID;

	switch (ep_address) {
	case ATH10K_USB_EP_ADDR_APP_CTRL_IN:
		pipe_num = ATH10K_USB_PIPE_RX_CTRL;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH10K_USB_EP_ADDR_APP_DATA_IN:
		pipe_num = ATH10K_USB_PIPE_RX_DATA;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH10K_USB_EP_ADDR_APP_INT_IN:
		pipe_num = ATH10K_USB_PIPE_RX_INT;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH10K_USB_EP_ADDR_APP_DATA2_IN:
		pipe_num = ATH10K_USB_PIPE_RX_DATA2;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH10K_USB_EP_ADDR_APP_CTRL_OUT:
		pipe_num = ATH10K_USB_PIPE_TX_CTRL;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH10K_USB_EP_ADDR_APP_DATA_LP_OUT:
		pipe_num = ATH10K_USB_PIPE_TX_DATA_LP;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH10K_USB_EP_ADDR_APP_DATA_MP_OUT:
		pipe_num = ATH10K_USB_PIPE_TX_DATA_MP;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH10K_USB_EP_ADDR_APP_DATA_HP_OUT:
		pipe_num = ATH10K_USB_PIPE_TX_DATA_HP;
		*urb_count = TX_URB_COUNT;
		break;
	default:
		/* note: there may be endpoints not currently used */
		break;
	}

	return pipe_num;
}

static int ath10k_usb_alloc_pipe_resources(struct ath10k *ar,
					   struct ath10k_usb_pipe *pipe,
					   int urb_cnt)
{
	struct ath10k_urb_context *urb_context;
	int i;

	INIT_LIST_HEAD(&pipe->urb_list_head);
	init_usb_anchor(&pipe->urb_submitted);

	for (i = 0; i < urb_cnt; i++) {
		urb_context = kzalloc(sizeof(*urb_context), GFP_KERNEL);
		if (!urb_context)
			return -ENOMEM;

		urb_context->pipe = pipe;

		/* we are only allocate the urb contexts here, the actual URB
		 * is allocated from the kernel as needed to do a transaction
		 */
		pipe->urb_alloc++;
		ath10k_usb_free_urb_to_pipe(pipe, urb_context);
	}

	ath10k_dbg(ar, ATH10K_DBG_USB,
		   "usb alloc resources lpipe %d hpipe 0x%x urbs %d\n",
		   pipe->logical_pipe_num, pipe->usb_pipe_handle,
		   pipe->urb_alloc);

	return 0;
}

static int ath10k_usb_setup_pipe_resources(struct ath10k *ar,
					   struct usb_interface *interface)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);
	struct usb_host_interface *iface_desc = interface->cur_altsetting;
	struct usb_endpoint_descriptor *endpoint;
	struct ath10k_usb_pipe *pipe;
	int ret, i, urbcount;
	u8 pipe_num;

	ath10k_dbg(ar, ATH10K_DBG_USB, "usb setting up pipes using interface\n");

	/* walk decriptors and setup pipes */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (ATH10K_USB_IS_BULK_EP(endpoint->bmAttributes)) {
			ath10k_dbg(ar, ATH10K_DBG_USB,
				   "usb %s bulk ep 0x%2.2x maxpktsz %d\n",
				   ATH10K_USB_IS_DIR_IN
				   (endpoint->bEndpointAddress) ?
				   "rx" : "tx", endpoint->bEndpointAddress,
				   le16_to_cpu(endpoint->wMaxPacketSize));
		} else if (ATH10K_USB_IS_INT_EP(endpoint->bmAttributes)) {
			ath10k_dbg(ar, ATH10K_DBG_USB,
				   "usb %s int ep 0x%2.2x maxpktsz %d interval %d\n",
				   ATH10K_USB_IS_DIR_IN
				   (endpoint->bEndpointAddress) ?
				   "rx" : "tx", endpoint->bEndpointAddress,
				   le16_to_cpu(endpoint->wMaxPacketSize),
				   endpoint->bInterval);
		} else if (ATH10K_USB_IS_ISOC_EP(endpoint->bmAttributes)) {
			/* TODO for ISO */
			ath10k_dbg(ar, ATH10K_DBG_USB,
				   "usb %s isoc ep 0x%2.2x maxpktsz %d interval %d\n",
				   ATH10K_USB_IS_DIR_IN
				   (endpoint->bEndpointAddress) ?
				   "rx" : "tx", endpoint->bEndpointAddress,
				   le16_to_cpu(endpoint->wMaxPacketSize),
				   endpoint->bInterval);
		}
		urbcount = 0;

		pipe_num =
		    ath10k_usb_get_logical_pipe_num(endpoint->bEndpointAddress,
						    &urbcount);
		if (pipe_num == ATH10K_USB_PIPE_INVALID)
			continue;

		pipe = &ar_usb->pipes[pipe_num];
		if (pipe->ar_usb)
			/* hmmm..pipe was already setup */
			continue;

		pipe->ar_usb = ar_usb;
		pipe->logical_pipe_num = pipe_num;
		pipe->ep_address = endpoint->bEndpointAddress;
		pipe->max_packet_size = le16_to_cpu(endpoint->wMaxPacketSize);

		if (ATH10K_USB_IS_BULK_EP(endpoint->bmAttributes)) {
			if (ATH10K_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle =
				    usb_rcvbulkpipe(ar_usb->udev,
						    pipe->ep_address);
			} else {
				pipe->usb_pipe_handle =
				    usb_sndbulkpipe(ar_usb->udev,
						    pipe->ep_address);
			}
		} else if (ATH10K_USB_IS_INT_EP(endpoint->bmAttributes)) {
			if (ATH10K_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle =
				    usb_rcvintpipe(ar_usb->udev,
						   pipe->ep_address);
			} else {
				pipe->usb_pipe_handle =
				    usb_sndintpipe(ar_usb->udev,
						   pipe->ep_address);
			}
		} else if (ATH10K_USB_IS_ISOC_EP(endpoint->bmAttributes)) {
			/* TODO for ISO */
			if (ATH10K_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle =
				    usb_rcvisocpipe(ar_usb->udev,
						    pipe->ep_address);
			} else {
				pipe->usb_pipe_handle =
				    usb_sndisocpipe(ar_usb->udev,
						    pipe->ep_address);
			}
		}

		pipe->ep_desc = endpoint;

		if (!ATH10K_USB_IS_DIR_IN(pipe->ep_address))
			pipe->flags |= ATH10K_USB_PIPE_FLAG_TX;

		ret = ath10k_usb_alloc_pipe_resources(ar, pipe, urbcount);
		if (ret)
			return ret;
	}

	return 0;
}

static int ath10k_usb_create(struct ath10k *ar,
			     struct usb_interface *interface)
{
	struct ath10k_usb *ar_usb = ath10k_usb_priv(ar);
	struct usb_device *dev = interface_to_usbdev(interface);
	struct ath10k_usb_pipe *pipe;
	int ret, i;

	usb_set_intfdata(interface, ar_usb);
	spin_lock_init(&ar_usb->cs_lock);
	ar_usb->udev = dev;
	ar_usb->interface = interface;

	for (i = 0; i < ATH10K_USB_PIPE_MAX; i++) {
		pipe = &ar_usb->pipes[i];
		INIT_WORK(&pipe->io_complete_work,
			  ath10k_usb_io_comp_work);
		skb_queue_head_init(&pipe->io_comp_queue);
	}

	ar_usb->diag_cmd_buffer = kzalloc(ATH10K_USB_MAX_DIAG_CMD, GFP_KERNEL);
	if (!ar_usb->diag_cmd_buffer) {
		ret = -ENOMEM;
		goto err;
	}

	ar_usb->diag_resp_buffer = kzalloc(ATH10K_USB_MAX_DIAG_RESP,
					   GFP_KERNEL);
	if (!ar_usb->diag_resp_buffer) {
		ret = -ENOMEM;
		goto err;
	}

	ret = ath10k_usb_setup_pipe_resources(ar, interface);
	if (ret)
		goto err;

	return 0;

err:
	ath10k_usb_destroy(ar);
	return ret;
}

/* ath10k usb driver registered functions */
static int ath10k_usb_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	struct ath10k *ar;
	struct ath10k_usb *ar_usb;
	struct usb_device *dev = interface_to_usbdev(interface);
	int ret, vendor_id, product_id;
	enum ath10k_hw_rev hw_rev;
	struct ath10k_bus_params bus_params = {};

	/* Assumption: All USB based chipsets (so far) are QCA9377 based.
	 * If there will be newer chipsets that does not use the hw reg
	 * setup as defined in qca6174_regs and qca6174_values, this
	 * assumption is no longer valid and hw_rev must be setup differently
	 * depending on chipset.
	 */
	hw_rev = ATH10K_HW_QCA9377;

	ar = ath10k_core_create(sizeof(*ar_usb), &dev->dev, ATH10K_BUS_USB,
				hw_rev, &ath10k_usb_hif_ops);
	if (!ar) {
		dev_err(&dev->dev, "failed to allocate core\n");
		return -ENOMEM;
	}

	usb_get_dev(dev);
	vendor_id = le16_to_cpu(dev->descriptor.idVendor);
	product_id = le16_to_cpu(dev->descriptor.idProduct);

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "usb new func vendor 0x%04x product 0x%04x\n",
		   vendor_id, product_id);

	ar_usb = ath10k_usb_priv(ar);
	ret = ath10k_usb_create(ar, interface);
	ar_usb->ar = ar;

	ar->dev_id = product_id;
	ar->id.vendor = vendor_id;
	ar->id.device = product_id;

	bus_params.dev_type = ATH10K_DEV_TYPE_HL;
	/* TODO: don't know yet how to get chip_id with USB */
	bus_params.chip_id = 0;
	ret = ath10k_core_register(ar, &bus_params);
	if (ret) {
		ath10k_warn(ar, "failed to register driver core: %d\n", ret);
		goto err;
	}

	/* TODO: remove this once USB support is fully implemented */
	ath10k_warn(ar, "Warning: ath10k USB support is incomplete, don't expect anything to work!\n");

	return 0;

err:
	ath10k_core_destroy(ar);

	usb_put_dev(dev);

	return ret;
}

static void ath10k_usb_remove(struct usb_interface *interface)
{
	struct ath10k_usb *ar_usb;

	ar_usb = usb_get_intfdata(interface);
	if (!ar_usb)
		return;

	ath10k_core_unregister(ar_usb->ar);
	ath10k_usb_destroy(ar_usb->ar);
	usb_put_dev(interface_to_usbdev(interface));
	ath10k_core_destroy(ar_usb->ar);
}

#ifdef CONFIG_PM

static int ath10k_usb_pm_suspend(struct usb_interface *interface,
				 pm_message_t message)
{
	struct ath10k_usb *ar_usb = usb_get_intfdata(interface);

	ath10k_usb_flush_all(ar_usb->ar);
	return 0;
}

static int ath10k_usb_pm_resume(struct usb_interface *interface)
{
	struct ath10k_usb *ar_usb = usb_get_intfdata(interface);
	struct ath10k *ar = ar_usb->ar;

	ath10k_usb_post_recv_transfers(ar,
				       &ar_usb->pipes[ATH10K_USB_PIPE_RX_DATA]);

	return 0;
}

#else

#define ath10k_usb_pm_suspend NULL
#define ath10k_usb_pm_resume NULL

#endif

/* table of devices that work with this driver */
static struct usb_device_id ath10k_usb_ids[] = {
	{USB_DEVICE(0x13b1, 0x0042)}, /* Linksys WUSB6100M */
	{ /* Terminating entry */ },
};

MODULE_DEVICE_TABLE(usb, ath10k_usb_ids);

static struct usb_driver ath10k_usb_driver = {
	.name = "ath10k_usb",
	.probe = ath10k_usb_probe,
	.suspend = ath10k_usb_pm_suspend,
	.resume = ath10k_usb_pm_resume,
	.disconnect = ath10k_usb_remove,
	.id_table = ath10k_usb_ids,
	.supports_autosuspend = true,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(ath10k_usb_driver);

MODULE_AUTHOR("Atheros Communications, Inc.");
MODULE_DESCRIPTION("Driver support for Qualcomm Atheros 802.11ac WLAN USB devices");
MODULE_LICENSE("Dual BSD/GPL");
