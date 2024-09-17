/*
 * Copyright (c) 2007-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/module.h>
#include <linux/usb.h>

#include "debug.h"
#include "core.h"

/* constants */
#define TX_URB_COUNT            32
#define RX_URB_COUNT            32
#define ATH6KL_USB_RX_BUFFER_SIZE  4096

/* tx/rx pipes for usb */
enum ATH6KL_USB_PIPE_ID {
	ATH6KL_USB_PIPE_TX_CTRL = 0,
	ATH6KL_USB_PIPE_TX_DATA_LP,
	ATH6KL_USB_PIPE_TX_DATA_MP,
	ATH6KL_USB_PIPE_TX_DATA_HP,
	ATH6KL_USB_PIPE_RX_CTRL,
	ATH6KL_USB_PIPE_RX_DATA,
	ATH6KL_USB_PIPE_RX_DATA2,
	ATH6KL_USB_PIPE_RX_INT,
	ATH6KL_USB_PIPE_MAX
};

#define ATH6KL_USB_PIPE_INVALID ATH6KL_USB_PIPE_MAX

struct ath6kl_usb_pipe {
	struct list_head urb_list_head;
	struct usb_anchor urb_submitted;
	u32 urb_alloc;
	u32 urb_cnt;
	u32 urb_cnt_thresh;
	unsigned int usb_pipe_handle;
	u32 flags;
	u8 ep_address;
	u8 logical_pipe_num;
	struct ath6kl_usb *ar_usb;
	u16 max_packet_size;
	struct work_struct io_complete_work;
	struct sk_buff_head io_comp_queue;
	struct usb_endpoint_descriptor *ep_desc;
};

#define ATH6KL_USB_PIPE_FLAG_TX    (1 << 0)

/* usb device object */
struct ath6kl_usb {
	/* protects pipe->urb_list_head and  pipe->urb_cnt */
	spinlock_t cs_lock;

	struct usb_device *udev;
	struct usb_interface *interface;
	struct ath6kl_usb_pipe pipes[ATH6KL_USB_PIPE_MAX];
	u8 *diag_cmd_buffer;
	u8 *diag_resp_buffer;
	struct ath6kl *ar;
	struct workqueue_struct *wq;
};

/* usb urb object */
struct ath6kl_urb_context {
	struct list_head link;
	struct ath6kl_usb_pipe *pipe;
	struct sk_buff *skb;
	struct ath6kl *ar;
};

/* USB endpoint definitions */
#define ATH6KL_USB_EP_ADDR_APP_CTRL_IN          0x81
#define ATH6KL_USB_EP_ADDR_APP_DATA_IN          0x82
#define ATH6KL_USB_EP_ADDR_APP_DATA2_IN         0x83
#define ATH6KL_USB_EP_ADDR_APP_INT_IN           0x84

#define ATH6KL_USB_EP_ADDR_APP_CTRL_OUT         0x01
#define ATH6KL_USB_EP_ADDR_APP_DATA_LP_OUT      0x02
#define ATH6KL_USB_EP_ADDR_APP_DATA_MP_OUT      0x03
#define ATH6KL_USB_EP_ADDR_APP_DATA_HP_OUT      0x04

/* diagnostic command defnitions */
#define ATH6KL_USB_CONTROL_REQ_SEND_BMI_CMD        1
#define ATH6KL_USB_CONTROL_REQ_RECV_BMI_RESP       2
#define ATH6KL_USB_CONTROL_REQ_DIAG_CMD            3
#define ATH6KL_USB_CONTROL_REQ_DIAG_RESP           4

#define ATH6KL_USB_CTRL_DIAG_CC_READ               0
#define ATH6KL_USB_CTRL_DIAG_CC_WRITE              1

struct ath6kl_usb_ctrl_diag_cmd_write {
	__le32 cmd;
	__le32 address;
	__le32 value;
	__le32 _pad[1];
} __packed;

struct ath6kl_usb_ctrl_diag_cmd_read {
	__le32 cmd;
	__le32 address;
} __packed;

struct ath6kl_usb_ctrl_diag_resp_read {
	__le32 value;
} __packed;

/* function declarations */
static void ath6kl_usb_recv_complete(struct urb *urb);

#define ATH6KL_USB_IS_BULK_EP(attr) (((attr) & 3) == 0x02)
#define ATH6KL_USB_IS_INT_EP(attr)  (((attr) & 3) == 0x03)
#define ATH6KL_USB_IS_ISOC_EP(attr)  (((attr) & 3) == 0x01)
#define ATH6KL_USB_IS_DIR_IN(addr)  ((addr) & 0x80)

/* pipe/urb operations */
static struct ath6kl_urb_context *
ath6kl_usb_alloc_urb_from_pipe(struct ath6kl_usb_pipe *pipe)
{
	struct ath6kl_urb_context *urb_context = NULL;
	unsigned long flags;

	/* bail if this pipe is not initialized */
	if (!pipe->ar_usb)
		return NULL;

	spin_lock_irqsave(&pipe->ar_usb->cs_lock, flags);
	if (!list_empty(&pipe->urb_list_head)) {
		urb_context =
		    list_first_entry(&pipe->urb_list_head,
				     struct ath6kl_urb_context, link);
		list_del(&urb_context->link);
		pipe->urb_cnt--;
	}
	spin_unlock_irqrestore(&pipe->ar_usb->cs_lock, flags);

	return urb_context;
}

static void ath6kl_usb_free_urb_to_pipe(struct ath6kl_usb_pipe *pipe,
					struct ath6kl_urb_context *urb_context)
{
	unsigned long flags;

	/* bail if this pipe is not initialized */
	if (!pipe->ar_usb)
		return;

	spin_lock_irqsave(&pipe->ar_usb->cs_lock, flags);
	pipe->urb_cnt++;

	list_add(&urb_context->link, &pipe->urb_list_head);
	spin_unlock_irqrestore(&pipe->ar_usb->cs_lock, flags);
}

static void ath6kl_usb_cleanup_recv_urb(struct ath6kl_urb_context *urb_context)
{
	dev_kfree_skb(urb_context->skb);
	urb_context->skb = NULL;

	ath6kl_usb_free_urb_to_pipe(urb_context->pipe, urb_context);
}

static inline struct ath6kl_usb *ath6kl_usb_priv(struct ath6kl *ar)
{
	return ar->hif_priv;
}

/* pipe resource allocation/cleanup */
static int ath6kl_usb_alloc_pipe_resources(struct ath6kl_usb_pipe *pipe,
					   int urb_cnt)
{
	struct ath6kl_urb_context *urb_context;
	int status = 0, i;

	INIT_LIST_HEAD(&pipe->urb_list_head);
	init_usb_anchor(&pipe->urb_submitted);

	for (i = 0; i < urb_cnt; i++) {
		urb_context = kzalloc(sizeof(struct ath6kl_urb_context),
				      GFP_KERNEL);
		if (urb_context == NULL) {
			status = -ENOMEM;
			goto fail_alloc_pipe_resources;
		}

		urb_context->pipe = pipe;

		/*
		 * we are only allocate the urb contexts here, the actual URB
		 * is allocated from the kernel as needed to do a transaction
		 */
		pipe->urb_alloc++;
		ath6kl_usb_free_urb_to_pipe(pipe, urb_context);
	}

	ath6kl_dbg(ATH6KL_DBG_USB,
		   "ath6kl usb: alloc resources lpipe:%d hpipe:0x%X urbs:%d\n",
		   pipe->logical_pipe_num, pipe->usb_pipe_handle,
		   pipe->urb_alloc);

fail_alloc_pipe_resources:
	return status;
}

static void ath6kl_usb_free_pipe_resources(struct ath6kl_usb_pipe *pipe)
{
	struct ath6kl_urb_context *urb_context;

	if (pipe->ar_usb == NULL) {
		/* nothing allocated for this pipe */
		return;
	}

	ath6kl_dbg(ATH6KL_DBG_USB,
		   "ath6kl usb: free resources lpipe:%d"
		   "hpipe:0x%X urbs:%d avail:%d\n",
		   pipe->logical_pipe_num, pipe->usb_pipe_handle,
		   pipe->urb_alloc, pipe->urb_cnt);

	if (pipe->urb_alloc != pipe->urb_cnt) {
		ath6kl_dbg(ATH6KL_DBG_USB,
			   "ath6kl usb: urb leak! lpipe:%d"
			   "hpipe:0x%X urbs:%d avail:%d\n",
			   pipe->logical_pipe_num, pipe->usb_pipe_handle,
			   pipe->urb_alloc, pipe->urb_cnt);
	}

	while (true) {
		urb_context = ath6kl_usb_alloc_urb_from_pipe(pipe);
		if (urb_context == NULL)
			break;
		kfree(urb_context);
	}
}

static void ath6kl_usb_cleanup_pipe_resources(struct ath6kl_usb *ar_usb)
{
	int i;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++)
		ath6kl_usb_free_pipe_resources(&ar_usb->pipes[i]);
}

static u8 ath6kl_usb_get_logical_pipe_num(struct ath6kl_usb *ar_usb,
					  u8 ep_address, int *urb_count)
{
	u8 pipe_num = ATH6KL_USB_PIPE_INVALID;

	switch (ep_address) {
	case ATH6KL_USB_EP_ADDR_APP_CTRL_IN:
		pipe_num = ATH6KL_USB_PIPE_RX_CTRL;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA_IN:
		pipe_num = ATH6KL_USB_PIPE_RX_DATA;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_INT_IN:
		pipe_num = ATH6KL_USB_PIPE_RX_INT;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA2_IN:
		pipe_num = ATH6KL_USB_PIPE_RX_DATA2;
		*urb_count = RX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_CTRL_OUT:
		pipe_num = ATH6KL_USB_PIPE_TX_CTRL;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA_LP_OUT:
		pipe_num = ATH6KL_USB_PIPE_TX_DATA_LP;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA_MP_OUT:
		pipe_num = ATH6KL_USB_PIPE_TX_DATA_MP;
		*urb_count = TX_URB_COUNT;
		break;
	case ATH6KL_USB_EP_ADDR_APP_DATA_HP_OUT:
		pipe_num = ATH6KL_USB_PIPE_TX_DATA_HP;
		*urb_count = TX_URB_COUNT;
		break;
	default:
		/* note: there may be endpoints not currently used */
		break;
	}

	return pipe_num;
}

static int ath6kl_usb_setup_pipe_resources(struct ath6kl_usb *ar_usb)
{
	struct usb_interface *interface = ar_usb->interface;
	struct usb_host_interface *iface_desc = interface->cur_altsetting;
	struct usb_endpoint_descriptor *endpoint;
	struct ath6kl_usb_pipe *pipe;
	int i, urbcount, status = 0;
	u8 pipe_num;

	ath6kl_dbg(ATH6KL_DBG_USB, "setting up USB Pipes using interface\n");

	/* walk descriptors and setup pipes */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (ATH6KL_USB_IS_BULK_EP(endpoint->bmAttributes)) {
			ath6kl_dbg(ATH6KL_DBG_USB,
				   "%s Bulk Ep:0x%2.2X maxpktsz:%d\n",
				   ATH6KL_USB_IS_DIR_IN
				   (endpoint->bEndpointAddress) ?
				   "RX" : "TX", endpoint->bEndpointAddress,
				   le16_to_cpu(endpoint->wMaxPacketSize));
		} else if (ATH6KL_USB_IS_INT_EP(endpoint->bmAttributes)) {
			ath6kl_dbg(ATH6KL_DBG_USB,
				   "%s Int Ep:0x%2.2X maxpktsz:%d interval:%d\n",
				   ATH6KL_USB_IS_DIR_IN
				   (endpoint->bEndpointAddress) ?
				   "RX" : "TX", endpoint->bEndpointAddress,
				   le16_to_cpu(endpoint->wMaxPacketSize),
				   endpoint->bInterval);
		} else if (ATH6KL_USB_IS_ISOC_EP(endpoint->bmAttributes)) {
			/* TODO for ISO */
			ath6kl_dbg(ATH6KL_DBG_USB,
				   "%s ISOC Ep:0x%2.2X maxpktsz:%d interval:%d\n",
				   ATH6KL_USB_IS_DIR_IN
				   (endpoint->bEndpointAddress) ?
				   "RX" : "TX", endpoint->bEndpointAddress,
				   le16_to_cpu(endpoint->wMaxPacketSize),
				   endpoint->bInterval);
		}

		/* Ignore broken descriptors. */
		if (usb_endpoint_maxp(endpoint) == 0)
			continue;

		urbcount = 0;

		pipe_num =
		    ath6kl_usb_get_logical_pipe_num(ar_usb,
						    endpoint->bEndpointAddress,
						    &urbcount);
		if (pipe_num == ATH6KL_USB_PIPE_INVALID)
			continue;

		pipe = &ar_usb->pipes[pipe_num];
		if (pipe->ar_usb != NULL) {
			/* hmmm..pipe was already setup */
			continue;
		}

		pipe->ar_usb = ar_usb;
		pipe->logical_pipe_num = pipe_num;
		pipe->ep_address = endpoint->bEndpointAddress;
		pipe->max_packet_size = le16_to_cpu(endpoint->wMaxPacketSize);

		if (ATH6KL_USB_IS_BULK_EP(endpoint->bmAttributes)) {
			if (ATH6KL_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle =
				    usb_rcvbulkpipe(ar_usb->udev,
						    pipe->ep_address);
			} else {
				pipe->usb_pipe_handle =
				    usb_sndbulkpipe(ar_usb->udev,
						    pipe->ep_address);
			}
		} else if (ATH6KL_USB_IS_INT_EP(endpoint->bmAttributes)) {
			if (ATH6KL_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle =
				    usb_rcvintpipe(ar_usb->udev,
						   pipe->ep_address);
			} else {
				pipe->usb_pipe_handle =
				    usb_sndintpipe(ar_usb->udev,
						   pipe->ep_address);
			}
		} else if (ATH6KL_USB_IS_ISOC_EP(endpoint->bmAttributes)) {
			/* TODO for ISO */
			if (ATH6KL_USB_IS_DIR_IN(pipe->ep_address)) {
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

		if (!ATH6KL_USB_IS_DIR_IN(pipe->ep_address))
			pipe->flags |= ATH6KL_USB_PIPE_FLAG_TX;

		status = ath6kl_usb_alloc_pipe_resources(pipe, urbcount);
		if (status != 0)
			break;
	}

	return status;
}

/* pipe operations */
static void ath6kl_usb_post_recv_transfers(struct ath6kl_usb_pipe *recv_pipe,
					   int buffer_length)
{
	struct ath6kl_urb_context *urb_context;
	struct urb *urb;
	int usb_status;

	while (true) {
		urb_context = ath6kl_usb_alloc_urb_from_pipe(recv_pipe);
		if (urb_context == NULL)
			break;

		urb_context->skb = dev_alloc_skb(buffer_length);
		if (urb_context->skb == NULL)
			goto err_cleanup_urb;

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (urb == NULL)
			goto err_cleanup_urb;

		usb_fill_bulk_urb(urb,
				  recv_pipe->ar_usb->udev,
				  recv_pipe->usb_pipe_handle,
				  urb_context->skb->data,
				  buffer_length,
				  ath6kl_usb_recv_complete, urb_context);

		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
			   "ath6kl usb: bulk recv submit:%d, 0x%X (ep:0x%2.2X), %d bytes buf:0x%p\n",
			   recv_pipe->logical_pipe_num,
			   recv_pipe->usb_pipe_handle, recv_pipe->ep_address,
			   buffer_length, urb_context->skb);

		usb_anchor_urb(urb, &recv_pipe->urb_submitted);
		usb_status = usb_submit_urb(urb, GFP_ATOMIC);

		if (usb_status) {
			ath6kl_dbg(ATH6KL_DBG_USB_BULK,
				   "ath6kl usb : usb bulk recv failed %d\n",
				   usb_status);
			usb_unanchor_urb(urb);
			usb_free_urb(urb);
			goto err_cleanup_urb;
		}
		usb_free_urb(urb);
	}
	return;

err_cleanup_urb:
	ath6kl_usb_cleanup_recv_urb(urb_context);
	return;
}

static void ath6kl_usb_flush_all(struct ath6kl_usb *ar_usb)
{
	int i;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {
		if (ar_usb->pipes[i].ar_usb != NULL)
			usb_kill_anchored_urbs(&ar_usb->pipes[i].urb_submitted);
	}

	/*
	 * Flushing any pending I/O may schedule work this call will block
	 * until all scheduled work runs to completion.
	 */
	flush_workqueue(ar_usb->wq);
}

static void ath6kl_usb_start_recv_pipes(struct ath6kl_usb *ar_usb)
{
	/*
	 * note: control pipe is no longer used
	 * ar_usb->pipes[ATH6KL_USB_PIPE_RX_CTRL].urb_cnt_thresh =
	 *      ar_usb->pipes[ATH6KL_USB_PIPE_RX_CTRL].urb_alloc/2;
	 * ath6kl_usb_post_recv_transfers(&ar_usb->
	 *		pipes[ATH6KL_USB_PIPE_RX_CTRL],
	 *		ATH6KL_USB_RX_BUFFER_SIZE);
	 */

	ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA].urb_cnt_thresh = 1;

	ath6kl_usb_post_recv_transfers(&ar_usb->pipes[ATH6KL_USB_PIPE_RX_DATA],
				       ATH6KL_USB_RX_BUFFER_SIZE);
}

/* hif usb rx/tx completion functions */
static void ath6kl_usb_recv_complete(struct urb *urb)
{
	struct ath6kl_urb_context *urb_context = urb->context;
	struct ath6kl_usb_pipe *pipe = urb_context->pipe;
	struct sk_buff *skb = NULL;
	int status = 0;

	ath6kl_dbg(ATH6KL_DBG_USB_BULK,
		   "%s: recv pipe: %d, stat:%d, len:%d urb:0x%p\n", __func__,
		   pipe->logical_pipe_num, urb->status, urb->actual_length,
		   urb);

	if (urb->status != 0) {
		status = -EIO;
		switch (urb->status) {
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/*
			 * no need to spew these errors when device
			 * removed or urb killed due to driver shutdown
			 */
			status = -ECANCELED;
			break;
		default:
			ath6kl_dbg(ATH6KL_DBG_USB_BULK,
				   "%s recv pipe: %d (ep:0x%2.2X), failed:%d\n",
				   __func__, pipe->logical_pipe_num,
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
	queue_work(pipe->ar_usb->wq, &pipe->io_complete_work);

cleanup_recv_urb:
	ath6kl_usb_cleanup_recv_urb(urb_context);

	if (status == 0 &&
	    pipe->urb_cnt >= pipe->urb_cnt_thresh) {
		/* our free urbs are piling up, post more transfers */
		ath6kl_usb_post_recv_transfers(pipe, ATH6KL_USB_RX_BUFFER_SIZE);
	}
}

static void ath6kl_usb_usb_transmit_complete(struct urb *urb)
{
	struct ath6kl_urb_context *urb_context = urb->context;
	struct ath6kl_usb_pipe *pipe = urb_context->pipe;
	struct sk_buff *skb;

	ath6kl_dbg(ATH6KL_DBG_USB_BULK,
		   "%s: pipe: %d, stat:%d, len:%d\n",
		   __func__, pipe->logical_pipe_num, urb->status,
		   urb->actual_length);

	if (urb->status != 0) {
		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
			   "%s:  pipe: %d, failed:%d\n",
			   __func__, pipe->logical_pipe_num, urb->status);
	}

	skb = urb_context->skb;
	urb_context->skb = NULL;
	ath6kl_usb_free_urb_to_pipe(urb_context->pipe, urb_context);

	/* note: queue implements a lock */
	skb_queue_tail(&pipe->io_comp_queue, skb);
	queue_work(pipe->ar_usb->wq, &pipe->io_complete_work);
}

static void ath6kl_usb_io_comp_work(struct work_struct *work)
{
	struct ath6kl_usb_pipe *pipe = container_of(work,
						    struct ath6kl_usb_pipe,
						    io_complete_work);
	struct ath6kl_usb *ar_usb;
	struct sk_buff *skb;

	ar_usb = pipe->ar_usb;

	while ((skb = skb_dequeue(&pipe->io_comp_queue))) {
		if (pipe->flags & ATH6KL_USB_PIPE_FLAG_TX) {
			ath6kl_dbg(ATH6KL_DBG_USB_BULK,
				   "ath6kl usb xmit callback buf:0x%p\n", skb);
			ath6kl_core_tx_complete(ar_usb->ar, skb);
		} else {
			ath6kl_dbg(ATH6KL_DBG_USB_BULK,
				   "ath6kl usb recv callback buf:0x%p\n", skb);
			ath6kl_core_rx_complete(ar_usb->ar, skb,
						pipe->logical_pipe_num);
		}
	}
}

#define ATH6KL_USB_MAX_DIAG_CMD (sizeof(struct ath6kl_usb_ctrl_diag_cmd_write))
#define ATH6KL_USB_MAX_DIAG_RESP (sizeof(struct ath6kl_usb_ctrl_diag_resp_read))

static void ath6kl_usb_destroy(struct ath6kl_usb *ar_usb)
{
	ath6kl_usb_flush_all(ar_usb);

	ath6kl_usb_cleanup_pipe_resources(ar_usb);

	usb_set_intfdata(ar_usb->interface, NULL);

	kfree(ar_usb->diag_cmd_buffer);
	kfree(ar_usb->diag_resp_buffer);
	destroy_workqueue(ar_usb->wq);

	kfree(ar_usb);
}

static struct ath6kl_usb *ath6kl_usb_create(struct usb_interface *interface)
{
	struct usb_device *dev = interface_to_usbdev(interface);
	struct ath6kl_usb *ar_usb;
	struct ath6kl_usb_pipe *pipe;
	int status = 0;
	int i;

	/* ath6kl_usb_destroy() needs ar_usb != NULL && ar_usb->wq != NULL. */
	ar_usb = kzalloc(sizeof(struct ath6kl_usb), GFP_KERNEL);
	if (ar_usb == NULL)
		return NULL;
	ar_usb->wq = alloc_workqueue("ath6kl_wq", 0, 0);
	if (!ar_usb->wq) {
		kfree(ar_usb);
		return NULL;
	}

	usb_set_intfdata(interface, ar_usb);
	spin_lock_init(&(ar_usb->cs_lock));
	ar_usb->udev = dev;
	ar_usb->interface = interface;

	for (i = 0; i < ATH6KL_USB_PIPE_MAX; i++) {
		pipe = &ar_usb->pipes[i];
		INIT_WORK(&pipe->io_complete_work,
			  ath6kl_usb_io_comp_work);
		skb_queue_head_init(&pipe->io_comp_queue);
	}

	ar_usb->diag_cmd_buffer = kzalloc(ATH6KL_USB_MAX_DIAG_CMD, GFP_KERNEL);
	if (ar_usb->diag_cmd_buffer == NULL) {
		status = -ENOMEM;
		goto fail_ath6kl_usb_create;
	}

	ar_usb->diag_resp_buffer = kzalloc(ATH6KL_USB_MAX_DIAG_RESP,
					   GFP_KERNEL);
	if (ar_usb->diag_resp_buffer == NULL) {
		status = -ENOMEM;
		goto fail_ath6kl_usb_create;
	}

	status = ath6kl_usb_setup_pipe_resources(ar_usb);

fail_ath6kl_usb_create:
	if (status != 0) {
		ath6kl_usb_destroy(ar_usb);
		ar_usb = NULL;
	}
	return ar_usb;
}

static void ath6kl_usb_device_detached(struct usb_interface *interface)
{
	struct ath6kl_usb *ar_usb;

	ar_usb = usb_get_intfdata(interface);
	if (ar_usb == NULL)
		return;

	ath6kl_stop_txrx(ar_usb->ar);

	/* Delay to wait for the target to reboot */
	mdelay(20);
	ath6kl_core_cleanup(ar_usb->ar);
	ath6kl_usb_destroy(ar_usb);
}

/* exported hif usb APIs for htc pipe */
static void hif_start(struct ath6kl *ar)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);
	int i;

	ath6kl_usb_start_recv_pipes(device);

	/* set the TX resource avail threshold for each TX pipe */
	for (i = ATH6KL_USB_PIPE_TX_CTRL;
	     i <= ATH6KL_USB_PIPE_TX_DATA_HP; i++) {
		device->pipes[i].urb_cnt_thresh =
		    device->pipes[i].urb_alloc / 2;
	}
}

static int ath6kl_usb_send(struct ath6kl *ar, u8 PipeID,
			   struct sk_buff *hdr_skb, struct sk_buff *skb)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);
	struct ath6kl_usb_pipe *pipe = &device->pipes[PipeID];
	struct ath6kl_urb_context *urb_context;
	int usb_status, status = 0;
	struct urb *urb;
	u8 *data;
	u32 len;

	ath6kl_dbg(ATH6KL_DBG_USB_BULK, "+%s pipe : %d, buf:0x%p\n",
		   __func__, PipeID, skb);

	urb_context = ath6kl_usb_alloc_urb_from_pipe(pipe);

	if (urb_context == NULL) {
		/*
		 * TODO: it is possible to run out of urbs if
		 * 2 endpoints map to the same pipe ID
		 */
		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
			   "%s pipe:%d no urbs left. URB Cnt : %d\n",
			   __func__, PipeID, pipe->urb_cnt);
		status = -ENOMEM;
		goto fail_hif_send;
	}

	urb_context->skb = skb;

	data = skb->data;
	len = skb->len;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (urb == NULL) {
		status = -ENOMEM;
		ath6kl_usb_free_urb_to_pipe(urb_context->pipe,
					    urb_context);
		goto fail_hif_send;
	}

	usb_fill_bulk_urb(urb,
			  device->udev,
			  pipe->usb_pipe_handle,
			  data,
			  len,
			  ath6kl_usb_usb_transmit_complete, urb_context);

	if ((len % pipe->max_packet_size) == 0) {
		/* hit a max packet boundary on this pipe */
		urb->transfer_flags |= URB_ZERO_PACKET;
	}

	ath6kl_dbg(ATH6KL_DBG_USB_BULK,
		   "athusb bulk send submit:%d, 0x%X (ep:0x%2.2X), %d bytes\n",
		   pipe->logical_pipe_num, pipe->usb_pipe_handle,
		   pipe->ep_address, len);

	usb_anchor_urb(urb, &pipe->urb_submitted);
	usb_status = usb_submit_urb(urb, GFP_ATOMIC);

	if (usb_status) {
		ath6kl_dbg(ATH6KL_DBG_USB_BULK,
			   "ath6kl usb : usb bulk transmit failed %d\n",
			   usb_status);
		usb_unanchor_urb(urb);
		ath6kl_usb_free_urb_to_pipe(urb_context->pipe,
					    urb_context);
		status = -EINVAL;
	}
	usb_free_urb(urb);

fail_hif_send:
	return status;
}

static void hif_stop(struct ath6kl *ar)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);

	ath6kl_usb_flush_all(device);
}

static void ath6kl_usb_get_default_pipe(struct ath6kl *ar,
					u8 *ul_pipe, u8 *dl_pipe)
{
	*ul_pipe = ATH6KL_USB_PIPE_TX_CTRL;
	*dl_pipe = ATH6KL_USB_PIPE_RX_CTRL;
}

static int ath6kl_usb_map_service_pipe(struct ath6kl *ar, u16 svc_id,
				       u8 *ul_pipe, u8 *dl_pipe)
{
	int status = 0;

	switch (svc_id) {
	case HTC_CTRL_RSVD_SVC:
	case WMI_CONTROL_SVC:
		*ul_pipe = ATH6KL_USB_PIPE_TX_CTRL;
		/* due to large control packets, shift to data pipe */
		*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
		break;
	case WMI_DATA_BE_SVC:
	case WMI_DATA_BK_SVC:
		*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_LP;
		/*
		* Disable rxdata2 directly, it will be enabled
		* if FW enable rxdata2
		*/
		*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
		break;
	case WMI_DATA_VI_SVC:

		if (test_bit(ATH6KL_FW_CAPABILITY_MAP_LP_ENDPOINT,
			     ar->fw_capabilities))
			*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_LP;
		else
			*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_MP;
		/*
		* Disable rxdata2 directly, it will be enabled
		* if FW enable rxdata2
		*/
		*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
		break;
	case WMI_DATA_VO_SVC:

		if (test_bit(ATH6KL_FW_CAPABILITY_MAP_LP_ENDPOINT,
			     ar->fw_capabilities))
			*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_LP;
		else
			*ul_pipe = ATH6KL_USB_PIPE_TX_DATA_MP;
		/*
		* Disable rxdata2 directly, it will be enabled
		* if FW enable rxdata2
		*/
		*dl_pipe = ATH6KL_USB_PIPE_RX_DATA;
		break;
	default:
		status = -EPERM;
		break;
	}

	return status;
}

static u16 ath6kl_usb_get_free_queue_number(struct ath6kl *ar, u8 pipe_id)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);

	return device->pipes[pipe_id].urb_cnt;
}

static void hif_detach_htc(struct ath6kl *ar)
{
	struct ath6kl_usb *device = ath6kl_usb_priv(ar);

	ath6kl_usb_flush_all(device);
}

static int ath6kl_usb_submit_ctrl_out(struct ath6kl_usb *ar_usb,
				   u8 req, u16 value, u16 index, void *data,
				   u32 size)
{
	u8 *buf = NULL;
	int ret;

	if (size > 0) {
		buf = kmemdup(data, size, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;
	}

	/* note: if successful returns number of bytes transfered */
	ret = usb_control_msg(ar_usb->udev,
			      usb_sndctrlpipe(ar_usb->udev, 0),
			      req,
			      USB_DIR_OUT | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, value, index, buf,
			      size, 1000);

	if (ret < 0) {
		ath6kl_warn("Failed to submit usb control message: %d\n", ret);
		kfree(buf);
		return ret;
	}

	kfree(buf);

	return 0;
}

static int ath6kl_usb_submit_ctrl_in(struct ath6kl_usb *ar_usb,
				  u8 req, u16 value, u16 index, void *data,
				  u32 size)
{
	u8 *buf = NULL;
	int ret;

	if (size > 0) {
		buf = kmalloc(size, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;
	}

	/* note: if successful returns number of bytes transfered */
	ret = usb_control_msg(ar_usb->udev,
				 usb_rcvctrlpipe(ar_usb->udev, 0),
				 req,
				 USB_DIR_IN | USB_TYPE_VENDOR |
				 USB_RECIP_DEVICE, value, index, buf,
				 size, 2000);

	if (ret < 0) {
		ath6kl_warn("Failed to read usb control message: %d\n", ret);
		kfree(buf);
		return ret;
	}

	memcpy((u8 *) data, buf, size);

	kfree(buf);

	return 0;
}

static int ath6kl_usb_ctrl_msg_exchange(struct ath6kl_usb *ar_usb,
				     u8 req_val, u8 *req_buf, u32 req_len,
				     u8 resp_val, u8 *resp_buf, u32 *resp_len)
{
	int ret;

	/* send command */
	ret = ath6kl_usb_submit_ctrl_out(ar_usb, req_val, 0, 0,
					 req_buf, req_len);

	if (ret != 0)
		return ret;

	if (resp_buf == NULL) {
		/* no expected response */
		return ret;
	}

	/* get response */
	ret = ath6kl_usb_submit_ctrl_in(ar_usb, resp_val, 0, 0,
					resp_buf, *resp_len);

	return ret;
}

static int ath6kl_usb_diag_read32(struct ath6kl *ar, u32 address, u32 *data)
{
	struct ath6kl_usb *ar_usb = ar->hif_priv;
	struct ath6kl_usb_ctrl_diag_resp_read *resp;
	struct ath6kl_usb_ctrl_diag_cmd_read *cmd;
	u32 resp_len;
	int ret;

	cmd = (struct ath6kl_usb_ctrl_diag_cmd_read *) ar_usb->diag_cmd_buffer;

	memset(cmd, 0, sizeof(*cmd));
	cmd->cmd = ATH6KL_USB_CTRL_DIAG_CC_READ;
	cmd->address = cpu_to_le32(address);
	resp_len = sizeof(*resp);

	ret = ath6kl_usb_ctrl_msg_exchange(ar_usb,
				ATH6KL_USB_CONTROL_REQ_DIAG_CMD,
				(u8 *) cmd,
				sizeof(struct ath6kl_usb_ctrl_diag_cmd_write),
				ATH6KL_USB_CONTROL_REQ_DIAG_RESP,
				ar_usb->diag_resp_buffer, &resp_len);

	if (ret) {
		ath6kl_warn("diag read32 failed: %d\n", ret);
		return ret;
	}

	resp = (struct ath6kl_usb_ctrl_diag_resp_read *)
		ar_usb->diag_resp_buffer;

	*data = le32_to_cpu(resp->value);

	return ret;
}

static int ath6kl_usb_diag_write32(struct ath6kl *ar, u32 address, __le32 data)
{
	struct ath6kl_usb *ar_usb = ar->hif_priv;
	struct ath6kl_usb_ctrl_diag_cmd_write *cmd;
	int ret;

	cmd = (struct ath6kl_usb_ctrl_diag_cmd_write *) ar_usb->diag_cmd_buffer;

	memset(cmd, 0, sizeof(struct ath6kl_usb_ctrl_diag_cmd_write));
	cmd->cmd = cpu_to_le32(ATH6KL_USB_CTRL_DIAG_CC_WRITE);
	cmd->address = cpu_to_le32(address);
	cmd->value = data;

	ret = ath6kl_usb_ctrl_msg_exchange(ar_usb,
					   ATH6KL_USB_CONTROL_REQ_DIAG_CMD,
					   (u8 *) cmd,
					   sizeof(*cmd),
					   0, NULL, NULL);
	if (ret) {
		ath6kl_warn("diag_write32 failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int ath6kl_usb_bmi_read(struct ath6kl *ar, u8 *buf, u32 len)
{
	struct ath6kl_usb *ar_usb = ar->hif_priv;
	int ret;

	/* get response */
	ret = ath6kl_usb_submit_ctrl_in(ar_usb,
					ATH6KL_USB_CONTROL_REQ_RECV_BMI_RESP,
					0, 0, buf, len);
	if (ret) {
		ath6kl_err("Unable to read the bmi data from the device: %d\n",
			   ret);
		return ret;
	}

	return 0;
}

static int ath6kl_usb_bmi_write(struct ath6kl *ar, u8 *buf, u32 len)
{
	struct ath6kl_usb *ar_usb = ar->hif_priv;
	int ret;

	/* send command */
	ret = ath6kl_usb_submit_ctrl_out(ar_usb,
					 ATH6KL_USB_CONTROL_REQ_SEND_BMI_CMD,
					 0, 0, buf, len);
	if (ret) {
		ath6kl_err("unable to send the bmi data to the device: %d\n",
			   ret);
		return ret;
	}

	return 0;
}

static int ath6kl_usb_power_on(struct ath6kl *ar)
{
	hif_start(ar);
	return 0;
}

static int ath6kl_usb_power_off(struct ath6kl *ar)
{
	hif_detach_htc(ar);
	return 0;
}

static void ath6kl_usb_stop(struct ath6kl *ar)
{
	hif_stop(ar);
}

static void ath6kl_usb_cleanup_scatter(struct ath6kl *ar)
{
	/*
	 * USB doesn't support it. Just return.
	 */
	return;
}

static int ath6kl_usb_suspend(struct ath6kl *ar, struct cfg80211_wowlan *wow)
{
	/*
	 * cfg80211 suspend/WOW currently not supported for USB.
	 */
	return 0;
}

static int ath6kl_usb_resume(struct ath6kl *ar)
{
	/*
	 * cfg80211 resume currently not supported for USB.
	 */
	return 0;
}

static const struct ath6kl_hif_ops ath6kl_usb_ops = {
	.diag_read32 = ath6kl_usb_diag_read32,
	.diag_write32 = ath6kl_usb_diag_write32,
	.bmi_read = ath6kl_usb_bmi_read,
	.bmi_write = ath6kl_usb_bmi_write,
	.power_on = ath6kl_usb_power_on,
	.power_off = ath6kl_usb_power_off,
	.stop = ath6kl_usb_stop,
	.pipe_send = ath6kl_usb_send,
	.pipe_get_default = ath6kl_usb_get_default_pipe,
	.pipe_map_service = ath6kl_usb_map_service_pipe,
	.pipe_get_free_queue_number = ath6kl_usb_get_free_queue_number,
	.cleanup_scatter = ath6kl_usb_cleanup_scatter,
	.suspend = ath6kl_usb_suspend,
	.resume = ath6kl_usb_resume,
};

/* ath6kl usb driver registered functions */
static int ath6kl_usb_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(interface);
	struct ath6kl *ar;
	struct ath6kl_usb *ar_usb = NULL;
	int vendor_id, product_id;
	int ret = 0;

	usb_get_dev(dev);

	vendor_id = le16_to_cpu(dev->descriptor.idVendor);
	product_id = le16_to_cpu(dev->descriptor.idProduct);

	ath6kl_dbg(ATH6KL_DBG_USB, "vendor_id = %04x\n", vendor_id);
	ath6kl_dbg(ATH6KL_DBG_USB, "product_id = %04x\n", product_id);

	if (interface->cur_altsetting)
		ath6kl_dbg(ATH6KL_DBG_USB, "USB Interface %d\n",
			   interface->cur_altsetting->desc.bInterfaceNumber);


	if (dev->speed == USB_SPEED_HIGH)
		ath6kl_dbg(ATH6KL_DBG_USB, "USB 2.0 Host\n");
	else
		ath6kl_dbg(ATH6KL_DBG_USB, "USB 1.1 Host\n");

	ar_usb = ath6kl_usb_create(interface);

	if (ar_usb == NULL) {
		ret = -ENOMEM;
		goto err_usb_put;
	}

	ar = ath6kl_core_create(&ar_usb->udev->dev);
	if (ar == NULL) {
		ath6kl_err("Failed to alloc ath6kl core\n");
		ret = -ENOMEM;
		goto err_usb_destroy;
	}

	ar->hif_priv = ar_usb;
	ar->hif_type = ATH6KL_HIF_TYPE_USB;
	ar->hif_ops = &ath6kl_usb_ops;
	ar->mbox_info.block_size = 16;
	ar->bmi.max_data_size = 252;

	ar_usb->ar = ar;

	ret = ath6kl_core_init(ar, ATH6KL_HTC_TYPE_PIPE);
	if (ret) {
		ath6kl_err("Failed to init ath6kl core: %d\n", ret);
		goto err_core_free;
	}

	return ret;

err_core_free:
	ath6kl_core_destroy(ar);
err_usb_destroy:
	ath6kl_usb_destroy(ar_usb);
err_usb_put:
	usb_put_dev(dev);

	return ret;
}

static void ath6kl_usb_remove(struct usb_interface *interface)
{
	usb_put_dev(interface_to_usbdev(interface));
	ath6kl_usb_device_detached(interface);
}

#ifdef CONFIG_PM

static int ath6kl_usb_pm_suspend(struct usb_interface *interface,
			      pm_message_t message)
{
	struct ath6kl_usb *device;
	device = usb_get_intfdata(interface);

	ath6kl_usb_flush_all(device);
	return 0;
}

static int ath6kl_usb_pm_resume(struct usb_interface *interface)
{
	struct ath6kl_usb *device;
	device = usb_get_intfdata(interface);

	ath6kl_usb_post_recv_transfers(&device->pipes[ATH6KL_USB_PIPE_RX_DATA],
				       ATH6KL_USB_RX_BUFFER_SIZE);
	ath6kl_usb_post_recv_transfers(&device->pipes[ATH6KL_USB_PIPE_RX_DATA2],
				       ATH6KL_USB_RX_BUFFER_SIZE);

	return 0;
}

#else

#define ath6kl_usb_pm_suspend NULL
#define ath6kl_usb_pm_resume NULL

#endif

/* table of devices that work with this driver */
static const struct usb_device_id ath6kl_usb_ids[] = {
	{USB_DEVICE(0x0cf3, 0x9375)},
	{USB_DEVICE(0x0cf3, 0x9374)},
	{USB_DEVICE(0x04da, 0x390d)},
	{ /* Terminating entry */ },
};

MODULE_DEVICE_TABLE(usb, ath6kl_usb_ids);

static struct usb_driver ath6kl_usb_driver = {
	.name = "ath6kl_usb",
	.probe = ath6kl_usb_probe,
	.suspend = ath6kl_usb_pm_suspend,
	.resume = ath6kl_usb_pm_resume,
	.disconnect = ath6kl_usb_remove,
	.id_table = ath6kl_usb_ids,
	.supports_autosuspend = true,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(ath6kl_usb_driver);

MODULE_AUTHOR("Atheros Communications, Inc.");
MODULE_DESCRIPTION("Driver support for Atheros AR600x USB devices");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_FIRMWARE(AR6004_HW_1_0_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6004_HW_1_0_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_0_DEFAULT_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_1_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6004_HW_1_1_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_1_DEFAULT_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_2_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6004_HW_1_2_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_2_DEFAULT_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_3_FW_DIR "/" AR6004_HW_1_3_FIRMWARE_FILE);
MODULE_FIRMWARE(AR6004_HW_1_3_BOARD_DATA_FILE);
MODULE_FIRMWARE(AR6004_HW_1_3_DEFAULT_BOARD_DATA_FILE);
