// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2018, 2020-2021, Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/termios.h>
#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/termios.h>
#include <linux/platform_device.h>

#include <soc/qcom/bam_dmux.h>

#include <linux/usb/msm_hsusb.h>
#include <linux/usb/usb_ctrl_qti.h>
#include <linux/usb_bam.h>

#include "u_rmnet.h"

static struct workqueue_struct *gbam_wq;
static unsigned int n_tx_req_queued;

static unsigned int bam_ch_ids[BAM_DMUX_NUM_FUNCS] = {
	BAM_DMUX_USB_RMNET_0,
	BAM_DMUX_USB_DPL
};

static char bam_ch_names[BAM_DMUX_NUM_FUNCS][BAM_DMUX_CH_NAME_MAX_LEN];

#define BAM_PENDING_PKTS_LIMIT			220
#define BAM_MUX_TX_PKT_DROP_THRESHOLD		1000
#define BAM_MUX_RX_PKT_FCTRL_EN_TSHOLD		500
#define BAM_MUX_RX_PKT_FCTRL_DIS_TSHOLD		300
#define BAM_MUX_RX_PKT_FLOW_CTRL_SUPPORT	1

#define BAM_MUX_HDR				8

#define BAM_MUX_RX_Q_SIZE			128
#define BAM_MUX_TX_Q_SIZE			200
#define BAM_MUX_RX_REQ_SIZE			2048   /* Must be 1KB aligned */

#define DL_INTR_THRESHOLD			20
#define BAM_PENDING_BYTES_LIMIT			(50 * BAM_MUX_RX_REQ_SIZE)
#define BAM_PENDING_BYTES_FCTRL_EN_TSHOLD	(BAM_PENDING_BYTES_LIMIT / 3)


static unsigned int bam_pending_pkts_limit = BAM_PENDING_PKTS_LIMIT;
module_param(bam_pending_pkts_limit, uint, 0644);

static unsigned int bam_pending_bytes_limit = BAM_PENDING_BYTES_LIMIT;
module_param(bam_pending_bytes_limit, uint, 0644);

static unsigned int bam_pending_bytes_fctrl_en_thold =
					BAM_PENDING_BYTES_FCTRL_EN_TSHOLD;
module_param(bam_pending_bytes_fctrl_en_thold, uint, 0644);

static unsigned int bam_mux_tx_pkt_drop_thld = BAM_MUX_TX_PKT_DROP_THRESHOLD;
module_param(bam_mux_tx_pkt_drop_thld, uint, 0644);

static unsigned int bam_mux_rx_fctrl_en_thld = BAM_MUX_RX_PKT_FCTRL_EN_TSHOLD;
module_param(bam_mux_rx_fctrl_en_thld, uint, 0644);

static unsigned int bam_mux_rx_fctrl_support = BAM_MUX_RX_PKT_FLOW_CTRL_SUPPORT;
module_param(bam_mux_rx_fctrl_support, uint, 0644);

static unsigned int bam_mux_rx_fctrl_dis_thld = BAM_MUX_RX_PKT_FCTRL_DIS_TSHOLD;
module_param(bam_mux_rx_fctrl_dis_thld, uint, 0644);

static unsigned int bam_mux_tx_q_size = BAM_MUX_TX_Q_SIZE;
module_param(bam_mux_tx_q_size, uint, 0644);

static unsigned int bam_mux_rx_q_size = BAM_MUX_RX_Q_SIZE;
module_param(bam_mux_rx_q_size, uint, 0644);

static unsigned long bam_mux_rx_req_size = BAM_MUX_RX_REQ_SIZE;
module_param(bam_mux_rx_req_size, ulong, 0444);

static unsigned int dl_intr_threshold = DL_INTR_THRESHOLD;
module_param(dl_intr_threshold, uint, 0644);

#define BAM_CH_OPENED			BIT(0)
#define BAM_CH_READY			BIT(1)
#define BAM_CH_WRITE_INPROGRESS		BIT(2)

enum u_bam_event_type {
	U_BAM_DISCONNECT_E = 0,
	U_BAM_CONNECT_E,
};

struct bam_ch_info {
	unsigned long		flags;
	unsigned int		id;

	struct list_head        tx_idle;
	struct sk_buff_head	tx_skb_q;

	struct list_head        rx_idle;
	struct sk_buff_head	rx_skb_q;
	struct sk_buff_head	rx_skb_idle;

	struct gbam_port	*port;
	struct work_struct	write_tobam_w;
	struct work_struct	write_tohost_w;

	/* stats */
	unsigned int		pending_pkts_with_bam;
	unsigned int		pending_bytes_with_bam;
	unsigned int		tohost_drp_cnt;
	unsigned int		tomodem_drp_cnt;
	unsigned int		tx_len;
	unsigned int		rx_len;
	unsigned long		to_modem;
	unsigned long		to_host;
	unsigned int		rx_flow_control_disable;
	unsigned int		rx_flow_control_enable;
	unsigned int		rx_flow_control_triggered;
	unsigned int		max_num_pkts_pending_with_bam;
	unsigned int		max_bytes_pending_with_bam;
	unsigned int		delayed_bam_mux_write_done;
	unsigned long		skb_expand_cnt;
};

struct gbam_port {
	enum u_bam_event_type	last_event;
	unsigned int		port_num;
	spinlock_t		port_lock_ul;
	spinlock_t		port_lock_dl;
	spinlock_t		port_lock;

	struct data_port	*port_usb;
	struct usb_gadget	*gadget;

	struct bam_ch_info	data_ch;

	struct work_struct	connect_w;
	struct work_struct	disconnect_w;
};

static struct bam_portmaster {
	struct gbam_port *port;
	struct platform_driver pdrv;
} bam_ports[BAM_DMUX_NUM_FUNCS];

static void gbam_start_rx(struct gbam_port *port);
static void gbam_notify(void *p, int event, unsigned long data);
static void gbam_data_write_tobam(struct work_struct *w);

/*---------------misc functions---------------- */
static void gbam_free_requests(struct usb_ep *ep, struct list_head *head)
{
	struct usb_request	*req;

	while (!list_empty(head)) {
		req = list_entry(head->next, struct usb_request, list);
		list_del(&req->list);
		usb_ep_free_request(ep, req);
	}
}

static int gbam_alloc_requests(struct usb_ep *ep, struct list_head *head,
		int num,
		void (*cb)(struct usb_ep *ep, struct usb_request *),
		gfp_t flags)
{
	int i;
	struct usb_request *req;

	pr_debug("%s: ep:%pK head:%pK num:%d cb:%pK\n", __func__,
			ep, head, num, cb);

	for (i = 0; i < num; i++) {
		req = usb_ep_alloc_request(ep, flags);
		if (!req) {
			pr_debug("%s: req allocated:%d\n", __func__, i);
			return list_empty(head) ? -ENOMEM : 0;
		}
		req->complete = cb;
		list_add(&req->list, head);
	}

	return 0;
}

static inline dma_addr_t gbam_get_dma_from_skb(struct sk_buff *skb)
{
	return *((dma_addr_t *)(skb->cb));
}

/* This function should be called with port_lock_ul lock held */
static struct sk_buff *gbam_alloc_skb_from_pool(struct gbam_port *port)
{
	struct bam_ch_info *d;
	struct sk_buff *skb;
	dma_addr_t      skb_buf_dma_addr;

	if (!port)
		return NULL;

	d = &port->data_ch;
	if (!d)
		return NULL;

	if (d->rx_skb_idle.qlen == 0) {
		/*
		 * In case skb idle pool is empty, we allow to allocate more
		 * skbs so we dynamically enlarge the pool size when needed.
		 * Therefore, in steady state this dynamic allocation will
		 * stop when the pool will arrive to its optimal size.
		 */
		pr_debug("%s: allocate skb\n", __func__);
		skb = alloc_skb(bam_mux_rx_req_size + BAM_MUX_HDR, GFP_ATOMIC);

		if (!skb)
			goto alloc_exit;

		skb_reserve(skb, BAM_MUX_HDR);
		skb_buf_dma_addr = DMA_MAPPING_ERROR;

		memcpy(skb->cb, &skb_buf_dma_addr,
			sizeof(skb_buf_dma_addr));

	} else {
		pr_debug("%s: pull skb from pool\n", __func__);
		skb = __skb_dequeue(&d->rx_skb_idle);
		if (!skb)
			goto alloc_exit;

		if (skb_headroom(skb) < BAM_MUX_HDR)
			skb_reserve(skb, BAM_MUX_HDR);
	}

alloc_exit:
	return skb;
}

/* This function should be called with port_lock_ul lock held */
static void gbam_free_skb_to_pool(struct gbam_port *port, struct sk_buff *skb)
{
	struct bam_ch_info *d;

	if (!port)
		return;
	d = &port->data_ch;

	skb->len = 0;
	skb_reset_tail_pointer(skb);
	__skb_queue_tail(&d->rx_skb_idle, skb);
}

static void gbam_free_rx_skb_idle_list(struct gbam_port *port)
{
	struct bam_ch_info *d;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	struct usb_gadget *gadget = NULL;

	if (!port)
		return;
	d = &port->data_ch;

	gadget = port->gadget;

	while (d->rx_skb_idle.qlen > 0) {
		skb = __skb_dequeue(&d->rx_skb_idle);
		if (!skb)
			break;

		dma_addr = gbam_get_dma_from_skb(skb);

		if (gadget && dma_addr != DMA_MAPPING_ERROR) {
			dma_unmap_single(&gadget->dev, dma_addr,
				bam_mux_rx_req_size, DMA_BIDIRECTIONAL);

			dma_addr = DMA_MAPPING_ERROR;
			memcpy(skb->cb, &dma_addr,
				sizeof(dma_addr));
		}
		dev_kfree_skb_any(skb);
	}
}

/*--------------------------------------------- */

/*------------data_path----------------------------*/
static void gbam_write_data_tohost(struct gbam_port *port)
{
	unsigned long			flags;
	struct bam_ch_info		*d = &port->data_ch;
	struct sk_buff			*skb;
	struct sk_buff			*new_skb;
	int				ret;
	int				tail_room = 0;
	int				extra_alloc = 0;
	struct usb_request		*req;
	struct usb_ep			*ep;

	spin_lock_irqsave(&port->port_lock_dl, flags);
	if (!port->port_usb) {
		spin_unlock_irqrestore(&port->port_lock_dl, flags);
		return;
	}

	ep = port->port_usb->in;

	while (!list_empty(&d->tx_idle)) {
		skb = __skb_dequeue(&d->tx_skb_q);
		if (!skb)
			break;

		tail_room = skb_tailroom(skb);
		if (tail_room < extra_alloc) {
			pr_debug("%s: tail_room  %d less than %d\n", __func__,
					tail_room, extra_alloc);
			new_skb = skb_copy_expand(skb, 0, extra_alloc -
					tail_room, GFP_ATOMIC);
			if (!new_skb) {
				pr_err("skb_copy_expand failed\n");
				break;
			}
			dev_kfree_skb_any(skb);
			skb = new_skb;
			d->skb_expand_cnt++;
		}

		req = list_first_entry(&d->tx_idle,
				struct usb_request,
				list);
		req->context = skb;
		req->buf = skb->data;
		req->length = skb->len;
		n_tx_req_queued++;
		if (n_tx_req_queued == dl_intr_threshold) {
			req->no_interrupt = 0;
			n_tx_req_queued = 0;
		} else {
			req->no_interrupt = 1;
		}

		/* Send ZLP in case packet length is multiple of maxpacksize */
		req->zero = 1;

		list_del(&req->list);

		spin_unlock(&port->port_lock_dl);
		ret = usb_ep_queue(ep, req, GFP_ATOMIC);
		spin_lock(&port->port_lock_dl);
		if (ret) {
			pr_err_ratelimited("%s: usb epIn failed with %d\n",
					 __func__, ret);
			list_add(&req->list, &d->tx_idle);
			dev_kfree_skb_any(skb);
			break;
		}
		d->to_host++;
	}
	spin_unlock_irqrestore(&port->port_lock_dl, flags);
}

static void gbam_write_data_tohost_w(struct work_struct *w)
{
	struct bam_ch_info	*d;
	struct gbam_port	*port;

	d = container_of(w, struct bam_ch_info, write_tohost_w);
	port = d->port;

	gbam_write_data_tohost(port);
}

static void gbam_data_recv_cb(void *p, struct sk_buff *skb)
{
	struct gbam_port	*port = p;
	struct bam_ch_info	*d = &port->data_ch;
	unsigned long		flags;

	if (!skb)
		return;

	pr_debug("%s: p:%pK#%d d:%pK skb_len:%d\n", __func__,
			port, port->port_num, d, skb->len);

	spin_lock_irqsave(&port->port_lock_dl, flags);
	if (!port->port_usb) {
		spin_unlock_irqrestore(&port->port_lock_dl, flags);
		dev_kfree_skb_any(skb);
		return;
	}

	if (d->tx_skb_q.qlen > bam_mux_tx_pkt_drop_thld) {
		d->tohost_drp_cnt++;
		pr_err_ratelimited("%s: tx pkt dropped: tx_drop_cnt:%u\n",
					__func__, d->tohost_drp_cnt);
		spin_unlock_irqrestore(&port->port_lock_dl, flags);
		dev_kfree_skb_any(skb);
		return;
	}

	__skb_queue_tail(&d->tx_skb_q, skb);
	spin_unlock_irqrestore(&port->port_lock_dl, flags);

	gbam_write_data_tohost(port);
}

static void gbam_data_write_done(void *p, struct sk_buff *skb)
{
	struct gbam_port	*port = p;
	struct bam_ch_info	*d = &port->data_ch;
	unsigned long		flags;

	if (!skb)
		return;

	spin_lock_irqsave(&port->port_lock_ul, flags);

	d->pending_pkts_with_bam--;
	d->pending_bytes_with_bam -= skb->len;
	gbam_free_skb_to_pool(port, skb);

	pr_debug("%s:port:%pK d:%pK tom:%lu ppkt:%u pbytes:%u pno:%d\n",
		       __func__, port, d, d->to_modem, d->pending_pkts_with_bam,
		       d->pending_bytes_with_bam, port->port_num);

	spin_unlock_irqrestore(&port->port_lock_ul, flags);

	/*
	 * If BAM doesn't have much pending data then push new data from here:
	 * write_complete notify only to avoid any underruns due to wq latency
	 */
	if (d->pending_bytes_with_bam <= bam_pending_bytes_fctrl_en_thold) {
		gbam_data_write_tobam(&d->write_tobam_w);
	} else {
		d->delayed_bam_mux_write_done++;
		queue_work(gbam_wq, &d->write_tobam_w);
	}
}

/* This function should be called with port_lock_ul spinlock acquired */
static bool gbam_ul_bam_limit_reached(struct bam_ch_info *data_ch)
{
	unsigned int	curr_pending_pkts = data_ch->pending_pkts_with_bam;
	unsigned int	curr_pending_bytes = data_ch->pending_bytes_with_bam;
	struct sk_buff	*skb;

	if (curr_pending_pkts >= bam_pending_pkts_limit)
		return true;

	/* check if next skb length doesn't exceed pending_bytes_limit */
	skb = skb_peek(&data_ch->rx_skb_q);
	if (!skb)
		return false;

	if ((curr_pending_bytes + skb->len) > bam_pending_bytes_limit)
		return true;
	else
		return false;
}

static void gbam_data_write_tobam(struct work_struct *w)
{
	struct gbam_port	*port;
	struct bam_ch_info	*d;
	struct sk_buff		*skb;
	unsigned long		flags;
	int			ret;
	int			qlen;

	d = container_of(w, struct bam_ch_info, write_tobam_w);
	port = d->port;

	spin_lock_irqsave(&port->port_lock_ul, flags);
	if (!port->port_usb) {
		spin_unlock_irqrestore(&port->port_lock_ul, flags);
		return;
	}
	/* Bail out if already in progress */
	if (test_bit(BAM_CH_WRITE_INPROGRESS, &d->flags)) {
		spin_unlock_irqrestore(&port->port_lock_ul, flags);
		return;
	}

	set_bit(BAM_CH_WRITE_INPROGRESS, &d->flags);

	while (!gbam_ul_bam_limit_reached(d)) {
		skb =  __skb_dequeue(&d->rx_skb_q);
		if (!skb)
			break;

		d->pending_pkts_with_bam++;
		d->pending_bytes_with_bam += skb->len;
		d->to_modem++;

		pr_debug("%s: port:%pK d:%pK tom:%lu ppkts:%u pbytes:%u pno:%d\n",
				__func__, port, d,
				d->to_modem, d->pending_pkts_with_bam,
				d->pending_bytes_with_bam, port->port_num);

		spin_unlock_irqrestore(&port->port_lock_ul, flags);
		ret = msm_bam_dmux_write(d->id, skb);
		spin_lock_irqsave(&port->port_lock_ul, flags);
		if (ret) {
			pr_debug("%s: write error:%d\n", __func__, ret);
			d->pending_pkts_with_bam--;
			d->pending_bytes_with_bam -= skb->len;
			d->to_modem--;
			d->tomodem_drp_cnt++;
			gbam_free_skb_to_pool(port, skb);
			break;
		}
		if (d->pending_pkts_with_bam > d->max_num_pkts_pending_with_bam)
			d->max_num_pkts_pending_with_bam =
					d->pending_pkts_with_bam;
		if (d->pending_bytes_with_bam > d->max_bytes_pending_with_bam)
			d->max_bytes_pending_with_bam =
					d->pending_bytes_with_bam;
	}

	qlen = d->rx_skb_q.qlen;

	clear_bit(BAM_CH_WRITE_INPROGRESS, &d->flags);
	spin_unlock_irqrestore(&port->port_lock_ul, flags);

	if (qlen < bam_mux_rx_fctrl_dis_thld) {
		if (d->rx_flow_control_triggered) {
			d->rx_flow_control_disable++;
			d->rx_flow_control_triggered = 0;
		}
		gbam_start_rx(port);
	}
}
/*-------------------------------------------------------------*/

static void gbam_epin_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct gbam_port	*port = ep->driver_data;
	struct bam_ch_info	*d;
	struct sk_buff		*skb = req->context;
	int			status = req->status;

	switch (status) {
	case 0:
		/* successful completion */
		break;
	case -ECONNRESET:
	case -ESHUTDOWN:
		/* connection gone */
		dev_kfree_skb_any(skb);
		usb_ep_free_request(ep, req);
		return;
	default:
		pr_err("%s: data tx ep error %d\n",
				__func__, status);
		break;
	}

	dev_kfree_skb_any(skb);

	if (!port)
		return;

	spin_lock(&port->port_lock_dl);
	d = &port->data_ch;
	list_add_tail(&req->list, &d->tx_idle);
	spin_unlock(&port->port_lock_dl);

	queue_work(gbam_wq, &d->write_tohost_w);
}

static void
gbam_epout_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct gbam_port	*port = ep->driver_data;
	struct bam_ch_info	*d = &port->data_ch;
	struct sk_buff		*skb = req->context;
	int			status = req->status;
	int			queue = 0;

	switch (status) {
	case 0:
		skb_put(skb, req->actual);
		queue = 1;
		break;
	case -ECONNRESET:
	case -ESHUTDOWN:
		/* cable disconnection */
		spin_lock(&port->port_lock_ul);
		gbam_free_skb_to_pool(port, skb);
		spin_unlock(&port->port_lock_ul);
		req->buf = NULL;
		usb_ep_free_request(ep, req);
		return;
	default:
		pr_err_ratelimited("%s: %s response error %d, %d/%d\n",
			__func__, ep->name, status, req->actual, req->length);
		spin_lock(&port->port_lock_ul);
		gbam_free_skb_to_pool(port, skb);
		spin_unlock(&port->port_lock_ul);
		break;
	}

	spin_lock(&port->port_lock_ul);

	if (queue) {
		__skb_queue_tail(&d->rx_skb_q, skb);
		queue_work(gbam_wq, &d->write_tobam_w);
	}

	/* TODO: Handle flow control gracefully by having
	 * call back mechanism from bam driver
	 */
	if (bam_mux_rx_fctrl_support &&
		d->rx_skb_q.qlen >= bam_mux_rx_fctrl_en_thld) {
		if (!d->rx_flow_control_triggered) {
			d->rx_flow_control_triggered = 1;
			d->rx_flow_control_enable++;
		}
		list_add_tail(&req->list, &d->rx_idle);
		spin_unlock(&port->port_lock_ul);
		return;
	}

	skb = gbam_alloc_skb_from_pool(port);
	if (!skb) {
		list_add_tail(&req->list, &d->rx_idle);
		spin_unlock(&port->port_lock_ul);
		return;
	}
	spin_unlock(&port->port_lock_ul);

	req->buf = skb->data;
	req->dma = gbam_get_dma_from_skb(skb);
	req->length = bam_mux_rx_req_size;

	req->context = skb;

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		spin_lock(&port->port_lock_ul);
		gbam_free_skb_to_pool(port, skb);
		spin_unlock(&port->port_lock_ul);

		pr_err_ratelimited("%s: data rx enqueue err %d\n",
					__func__, status);

		spin_lock(&port->port_lock_ul);
		list_add_tail(&req->list, &d->rx_idle);
		spin_unlock(&port->port_lock_ul);
	}
}

static void gbam_start_rx(struct gbam_port *port)
{
	struct usb_request		*req;
	struct bam_ch_info		*d;
	struct usb_ep			*ep;
	unsigned long			flags;
	int				ret;
	struct sk_buff			*skb;

	spin_lock_irqsave(&port->port_lock_ul, flags);
	if (!port->port_usb || !port->port_usb->out) {
		spin_unlock_irqrestore(&port->port_lock_ul, flags);
		return;
	}

	d = &port->data_ch;
	ep = port->port_usb->out;

	while (port->port_usb && !list_empty(&d->rx_idle)) {

		if (bam_mux_rx_fctrl_support &&
			d->rx_skb_q.qlen >= bam_mux_rx_fctrl_en_thld)
			break;

		req = list_first_entry(&d->rx_idle, struct usb_request, list);

		skb = gbam_alloc_skb_from_pool(port);
		if (!skb)
			break;

		list_del(&req->list);
		req->buf = skb->data;
		req->dma = gbam_get_dma_from_skb(skb);
		req->length = bam_mux_rx_req_size;

		req->context = skb;

		spin_unlock_irqrestore(&port->port_lock_ul, flags);
		ret = usb_ep_queue(ep, req, GFP_ATOMIC);
		spin_lock_irqsave(&port->port_lock_ul, flags);
		if (ret) {
			gbam_free_skb_to_pool(port, skb);

			pr_err_ratelimited("%s: rx queue failed %d\n",
							__func__, ret);

			if (port->port_usb)
				list_add(&req->list, &d->rx_idle);
			else
				usb_ep_free_request(ep, req);
			break;
		}
	}

	spin_unlock_irqrestore(&port->port_lock_ul, flags);
}

static int _gbam_start_io(struct gbam_port *port, bool in)
{
	unsigned long		flags;
	int			ret = 0;
	struct usb_ep		*ep;
	struct list_head	*idle;
	unsigned int		queue_size;
	spinlock_t		*spinlock;
	void		(*ep_complete)(struct usb_ep *ep, struct usb_request *req);

	if (in)
		spinlock = &port->port_lock_dl;
	else
		spinlock = &port->port_lock_ul;

	spin_lock_irqsave(spinlock, flags);
	if (!port->port_usb) {
		spin_unlock_irqrestore(spinlock, flags);
		return -EBUSY;
	}

	if (in) {
		ep = port->port_usb->in;
		idle = &port->data_ch.tx_idle;
		queue_size = bam_mux_tx_q_size;
		ep_complete = gbam_epin_complete;
	} else {
		ep = port->port_usb->out;
		if (!ep)
			goto out;
		idle = &port->data_ch.rx_idle;
		queue_size = bam_mux_rx_q_size;
		ep_complete = gbam_epout_complete;
	}

	ret = gbam_alloc_requests(ep, idle, queue_size, ep_complete,
			GFP_ATOMIC);
out:
	spin_unlock_irqrestore(spinlock, flags);
	if (ret)
		pr_err("%s: allocation failed\n", __func__);

	return ret;
}

static void gbam_start_io(struct gbam_port *port)
{
	unsigned long		flags;

	pr_debug("%s: port:%pK\n", __func__, port);

	if (_gbam_start_io(port, true))
		return;

	if (_gbam_start_io(port, false)) {
		spin_lock_irqsave(&port->port_lock_dl, flags);
		if (port->port_usb)
			gbam_free_requests(port->port_usb->in,
				&port->data_ch.tx_idle);
		spin_unlock_irqrestore(&port->port_lock_dl, flags);
		return;
	}

	/* queue out requests */
	gbam_start_rx(port);
}

static void gbam_notify(void *p, int event, unsigned long data)
{
	struct gbam_port	*port = p;
	struct bam_ch_info *d;
	struct sk_buff *skb;

	if (port == NULL)
		pr_err("BAM DMUX notifying after channel close\n");

	switch (event) {
	case BAM_DMUX_RECEIVE:
		skb = (struct sk_buff *)data;
		if (port)
			gbam_data_recv_cb(p, skb);
		else
			dev_kfree_skb_any(skb);
		break;
	case BAM_DMUX_WRITE_DONE:
		skb = (struct sk_buff *)data;
		if (port)
			gbam_data_write_done(p, skb);
		else
			dev_kfree_skb_any(skb);
		break;
	case BAM_DMUX_TRANSMIT_SIZE:
		d = &port->data_ch;
		if (test_bit(BAM_CH_OPENED, &d->flags))
			pr_warn("%s, BAM channel opened already\n", __func__);
		bam_mux_rx_req_size = data;
		pr_debug("%s rx_req_size: %lu\n", __func__, bam_mux_rx_req_size);
		break;
	}
}

static void gbam_free_rx_buffers(struct gbam_port *port)
{
	struct sk_buff		*skb;
	unsigned long		flags;
	struct bam_ch_info	*d;

	spin_lock_irqsave(&port->port_lock_ul, flags);

	if (!port->port_usb || !port->port_usb->out)
		goto free_rx_buf_out;

	d = &port->data_ch;
	gbam_free_requests(port->port_usb->out, &d->rx_idle);

	while ((skb = __skb_dequeue(&d->rx_skb_q)))
		dev_kfree_skb_any(skb);

	gbam_free_rx_skb_idle_list(port);

free_rx_buf_out:
	spin_unlock_irqrestore(&port->port_lock_ul, flags);
}

static void gbam_free_tx_buffers(struct gbam_port *port)
{
	struct sk_buff		*skb;
	unsigned long		flags;
	struct bam_ch_info	*d;

	spin_lock_irqsave(&port->port_lock_dl, flags);

	if (!port->port_usb)
		goto free_tx_buf_out;

	d = &port->data_ch;
	gbam_free_requests(port->port_usb->in, &d->tx_idle);

	while ((skb = __skb_dequeue(&d->tx_skb_q)))
		dev_kfree_skb_any(skb);

free_tx_buf_out:
	spin_unlock_irqrestore(&port->port_lock_dl, flags);
}

static void gbam_free_buffers(struct gbam_port *port)
{
	gbam_free_rx_buffers(port);
	gbam_free_tx_buffers(port);
}

static void gbam_disconnect_work(struct work_struct *w)
{
	struct gbam_port *port =
			container_of(w, struct gbam_port, disconnect_w);
	struct bam_ch_info *d = &port->data_ch;

	if (!test_bit(BAM_CH_OPENED, &d->flags)) {
		pr_err("%s: Bam channel is not opened\n", __func__);
		goto exit;
	}

	msm_bam_dmux_close(d->id);
	clear_bit(BAM_CH_OPENED, &d->flags);
exit:
	return;
}

static void gbam_connect_work(struct work_struct *w)
{
	struct gbam_port *port = container_of(w, struct gbam_port, connect_w);
	struct bam_ch_info *d = &port->data_ch;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&port->port_lock_ul, flags);
	spin_lock(&port->port_lock_dl);
	if (!port->port_usb) {
		spin_unlock(&port->port_lock_dl);
		spin_unlock_irqrestore(&port->port_lock_ul, flags);
		return;
	}
	spin_unlock(&port->port_lock_dl);
	spin_unlock_irqrestore(&port->port_lock_ul, flags);

	if (!test_bit(BAM_CH_READY, &d->flags)) {
		pr_err("%s: Bam channel is not ready\n", __func__);
		return;
	}

	ret = msm_bam_dmux_open(d->id, port, gbam_notify);
	if (ret) {
		pr_err("%s: unable open bam ch:%d err:%d\n",
				__func__, d->id, ret);
		return;
	}

	set_bit(BAM_CH_OPENED, &d->flags);

	gbam_start_io(port);
}

/* BAM data channel ready, allow attempt to open */
static int gbam_data_ch_probe(struct platform_device *pdev)
{
	struct gbam_port	*port;
	struct bam_ch_info	*d;
	int			i;
	unsigned long		flags;
	bool			do_work = false;

	pr_debug("%s: name:%s\n", __func__, pdev->name);

	for (i = 0; i < BAM_DMUX_NUM_FUNCS; i++) {
		port = bam_ports[i].port;
		if (!port)
			continue;

		d = &port->data_ch;

		if (!strcmp(bam_ch_names[i], pdev->name)) {
			set_bit(BAM_CH_READY, &d->flags);

			/* if usb is online, try opening bam_ch */
			spin_lock_irqsave(&port->port_lock_ul, flags);
			spin_lock(&port->port_lock_dl);
			if (port->port_usb)
				do_work = true;
			spin_unlock(&port->port_lock_dl);
			spin_unlock_irqrestore(&port->port_lock_ul, flags);

			if (do_work)
				queue_work(gbam_wq, &port->connect_w);
			break;
		}
	}

	return 0;
}

/* BAM data channel went inactive, so close it */
static int gbam_data_ch_remove(struct platform_device *pdev)
{
	struct gbam_port	*port;
	struct bam_ch_info	*d;
	struct usb_ep		*ep_in = NULL;
	struct usb_ep		*ep_out = NULL;
	unsigned long		flags;
	int			i;

	pr_debug("%s: name:%s\n", __func__, pdev->name);

	for (i = 0; i < BAM_DMUX_NUM_FUNCS; i++) {
		if (!strcmp(bam_ch_names[i], pdev->name)) {
			port = bam_ports[i].port;
			if (!port)
				continue;

			d = &port->data_ch;

			spin_lock_irqsave(&port->port_lock_ul, flags);
			spin_lock(&port->port_lock_dl);
			if (port->port_usb) {
				ep_in = port->port_usb->in;
				ep_out = port->port_usb->out;
			}
			spin_unlock(&port->port_lock_dl);
			spin_unlock_irqrestore(&port->port_lock_ul, flags);

			if (ep_in)
				usb_ep_fifo_flush(ep_in);
			if (ep_out)
				usb_ep_fifo_flush(ep_out);

			gbam_free_buffers(port);

			msm_bam_dmux_close(d->id);

			/* bam dmux will free all pending skbs */
			d->pending_pkts_with_bam = 0;
			d->pending_bytes_with_bam = 0;

			clear_bit(BAM_CH_READY, &d->flags);
			clear_bit(BAM_CH_OPENED, &d->flags);
		}
	}

	return 0;
}

static void gbam_port_free(enum bam_dmux_func_type func)
{
	struct gbam_port *port = bam_ports[func].port;
	struct platform_driver *pdrv = &bam_ports[func].pdrv;

	if (port) {
		platform_driver_unregister(pdrv);

		gbam_free_rx_skb_idle_list(port);
		kfree(port);
		bam_ports[func].port = NULL;
	}
}

static int gbam_port_alloc(enum bam_dmux_func_type func)
{
	struct gbam_port	*port;
	struct bam_ch_info	*d;
	struct platform_driver	*pdrv;

	port = kzalloc(sizeof(struct gbam_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->port_num = func;

	/* port initialization */
	spin_lock_init(&port->port_lock_ul);
	spin_lock_init(&port->port_lock_dl);
	spin_lock_init(&port->port_lock);
	INIT_WORK(&port->connect_w, gbam_connect_work);
	INIT_WORK(&port->disconnect_w, gbam_disconnect_work);

	/* data ch */
	d = &port->data_ch;
	d->port = port;
	INIT_LIST_HEAD(&d->tx_idle);
	INIT_LIST_HEAD(&d->rx_idle);
	INIT_WORK(&d->write_tobam_w, gbam_data_write_tobam);
	INIT_WORK(&d->write_tohost_w, gbam_write_data_tohost_w);
	skb_queue_head_init(&d->tx_skb_q);
	skb_queue_head_init(&d->rx_skb_q);
	skb_queue_head_init(&d->rx_skb_idle);
	d->id = bam_ch_ids[func];

	bam_ports[func].port = port;

	scnprintf(bam_ch_names[func], BAM_DMUX_CH_NAME_MAX_LEN,
			"bam_dmux_ch_%d", bam_ch_ids[func]);
	pdrv = &bam_ports[func].pdrv;
	pdrv->probe = gbam_data_ch_probe;
	pdrv->remove = gbam_data_ch_remove;
	pdrv->driver.name = bam_ch_names[func];
	pdrv->driver.owner = THIS_MODULE;

	platform_driver_register(pdrv);
	pr_debug("%s: port:%pK portno:%d\n", __func__, port, func);

	return 0;
}

#if defined(CONFIG_DEBUG_FS)
#define DEBUG_BUF_SIZE	1024
static ssize_t gbam_read_stats(struct file *file, char __user *ubuf,
		size_t count, loff_t *ppos)
{
	struct gbam_port	*port;
	struct bam_ch_info	*d;
	char			*buf;
	unsigned long		flags;
	int			ret;
	int			i;
	int			temp = 0;

	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < BAM_DMUX_NUM_FUNCS; i++) {
		port = bam_ports[i].port;
		if (!port)
			continue;
		spin_lock_irqsave(&port->port_lock_ul, flags);
		spin_lock(&port->port_lock_dl);

		d = &port->data_ch;

		temp += scnprintf(buf + temp, DEBUG_BUF_SIZE - temp,
				"#PORT:%d port:%pK data_ch:%pK#\n"
				"dpkts_to_usbhost: %lu\n"
				"dpkts_to_modem:  %lu\n"
				"dpkts_pwith_bam: %u\n"
				"dbytes_pwith_bam: %u\n"
				"to_usbhost_dcnt:  %u\n"
				"tomodem__dcnt:  %u\n"
				"rx_flow_control_disable_count: %u\n"
				"rx_flow_control_enable_count: %u\n"
				"rx_flow_control_triggered: %u\n"
				"max_num_pkts_pending_with_bam: %u\n"
				"max_bytes_pending_with_bam: %u\n"
				"delayed_bam_mux_write_done: %u\n"
				"tx_buf_len:	 %u\n"
				"rx_buf_len:	 %u\n"
				"data_ch_open:   %d\n"
				"data_ch_ready:  %d\n"
				"skb_expand_cnt: %lu\n",
				i, port, &port->data_ch,
				d->to_host, d->to_modem,
				d->pending_pkts_with_bam,
				d->pending_bytes_with_bam,
				d->tohost_drp_cnt, d->tomodem_drp_cnt,
				d->rx_flow_control_disable,
				d->rx_flow_control_enable,
				d->rx_flow_control_triggered,
				d->max_num_pkts_pending_with_bam,
				d->max_bytes_pending_with_bam,
				d->delayed_bam_mux_write_done,
				d->tx_skb_q.qlen, d->rx_skb_q.qlen,
				test_bit(BAM_CH_OPENED, &d->flags),
				test_bit(BAM_CH_READY, &d->flags),
				d->skb_expand_cnt);

		spin_unlock(&port->port_lock_dl);
		spin_unlock_irqrestore(&port->port_lock_ul, flags);
	}

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, temp);

	kfree(buf);

	return ret;
}

static ssize_t gbam_reset_stats(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct gbam_port	*port;
	struct bam_ch_info	*d;
	int			i;
	unsigned long		flags;

	for (i = 0; i < BAM_DMUX_NUM_FUNCS; i++) {
		port = bam_ports[i].port;
		if (!port)
			continue;

		spin_lock_irqsave(&port->port_lock_ul, flags);
		spin_lock(&port->port_lock_dl);

		d = &port->data_ch;

		d->to_host = 0;
		d->to_modem = 0;
		d->pending_pkts_with_bam = 0;
		d->pending_bytes_with_bam = 0;
		d->tohost_drp_cnt = 0;
		d->tomodem_drp_cnt = 0;
		d->rx_flow_control_disable = 0;
		d->rx_flow_control_enable = 0;
		d->rx_flow_control_triggered = 0;
		d->max_num_pkts_pending_with_bam = 0;
		d->max_bytes_pending_with_bam = 0;
		d->delayed_bam_mux_write_done = 0;
		d->skb_expand_cnt = 0;

		spin_unlock(&port->port_lock_dl);
		spin_unlock_irqrestore(&port->port_lock_ul, flags);
	}
	return count;
}

static const struct file_operations gbam_stats_ops = {
	.read = gbam_read_stats,
	.write = gbam_reset_stats,
};

static struct dentry *gbam_dent;
static void gbam_debugfs_init(void)
{
	struct dentry *dfile;

	if (gbam_dent)
		return;

	gbam_dent = debugfs_create_dir("usb_rmnet", NULL);
	if (!gbam_dent || IS_ERR(gbam_dent))
		return;

	dfile = debugfs_create_file("status", 0444, gbam_dent, NULL,
			&gbam_stats_ops);
	if (!dfile || IS_ERR(dfile)) {
		debugfs_remove(gbam_dent);
		gbam_dent = NULL;
		return;
	}
}
static void gbam_debugfs_remove(void)
{
	if (!gbam_dent)
		return;

	debugfs_remove(gbam_dent);
	gbam_dent = NULL;
}
#else
static inline void gbam_debugfs_init(void) {}
static inline void gbam_debugfs_remove(void) {}
#endif

void gbam_disconnect(struct data_port *gr, enum bam_dmux_func_type func)
{
	struct gbam_port	*port;
	unsigned long		flags, flags_ul;
	struct bam_ch_info	*d;

	pr_debug("%s: grmnet:%pK port#%d\n", __func__, gr, func);

	if (func >= BAM_DMUX_NUM_FUNCS) {
		pr_err("%s: invalid bam portno#%d\n", __func__, func);
		return;
	}

	if (!gr) {
		pr_err("%s: grmnet port is null\n", __func__);
		return;
	}
	port = bam_ports[func].port;

	if (!port) {
		pr_err("%s: NULL port\n", __func__);
		return;
	}

	spin_lock_irqsave(&port->port_lock, flags);

	d = &port->data_ch;
	/* Already disconnected due to suspend with remote wake disabled */
	if (port->last_event == U_BAM_DISCONNECT_E) {
		spin_unlock_irqrestore(&port->port_lock, flags);
		return;
	}

	port->port_usb = gr;

	gbam_free_buffers(port);

	spin_lock_irqsave(&port->port_lock_ul, flags_ul);
	spin_lock(&port->port_lock_dl);
	port->port_usb = NULL;
	n_tx_req_queued = 0;
	spin_unlock(&port->port_lock_dl);
	spin_unlock_irqrestore(&port->port_lock_ul, flags_ul);

	usb_ep_disable(gr->in);
	/* disable endpoints */
	if (gr->out)
		usb_ep_disable(gr->out);

	gr->in->driver_data = NULL;
	if (gr->out)
		gr->out->driver_data = NULL;

	port->last_event = U_BAM_DISCONNECT_E;
	queue_work(gbam_wq, &port->disconnect_w);

	spin_unlock_irqrestore(&port->port_lock, flags);
}

int gbam_connect(struct data_port *gr, enum bam_dmux_func_type func)
{
	struct gbam_port	*port;
	struct bam_ch_info	*d;
	int			ret;
	unsigned long		flags, flags_ul;

	pr_debug("%s: grmnet:%pK port#%d\n", __func__, gr, func);

	if (!gr) {
		pr_err("%s: grmnet port is null\n", __func__);
		return -ENODEV;
	}

	if (!gr->cdev->gadget) {
		pr_err("%s: gadget handle not passed\n", __func__);
		return -EINVAL;
	}

	if (func >= BAM_DMUX_NUM_FUNCS) {
		pr_err("%s: invalid portno#%d\n", __func__, func);
		return -ENODEV;
	}

	port = bam_ports[func].port;

	if (!port) {
		pr_err("%s: NULL port\n", __func__);
		return -ENODEV;
	}

	spin_lock_irqsave(&port->port_lock, flags);

	d = &port->data_ch;

	spin_lock_irqsave(&port->port_lock_ul, flags_ul);
	spin_lock(&port->port_lock_dl);
	port->port_usb = gr;
	port->gadget = port->port_usb->cdev->gadget;

	d->to_host = 0;
	d->to_modem = 0;
	d->pending_pkts_with_bam = 0;
	d->pending_bytes_with_bam = 0;
	d->tohost_drp_cnt = 0;
	d->tomodem_drp_cnt = 0;
	d->rx_flow_control_disable = 0;
	d->rx_flow_control_enable = 0;
	d->rx_flow_control_triggered = 0;
	d->max_num_pkts_pending_with_bam = 0;
	d->max_bytes_pending_with_bam = 0;
	d->delayed_bam_mux_write_done = 0;

	spin_unlock(&port->port_lock_dl);
	spin_unlock_irqrestore(&port->port_lock_ul, flags_ul);

	ret = usb_ep_enable(gr->in);
	if (ret) {
		pr_err("%s: usb_ep_enable failed eptype:IN ep:%pK\n",
			__func__, gr->in);
		goto exit;
	}
	gr->in->driver_data = port;

	/*
	 * DPL traffic is routed through BAM-DMUX on some targets.
	 * DPL function has only 1 IN endpoint. Add out endpoint
	 * checks for BAM-DMUX transport.
	 */
	if (gr->out) {
		ret = usb_ep_enable(gr->out);
		if (ret) {
			pr_err("%s: usb_ep_enable failed eptype:OUT ep:%pK\n",
					__func__, gr->out);
			gr->in->driver_data = NULL;
			usb_ep_disable(gr->in);
			goto exit;
		}
		gr->out->driver_data = port;
	}

	port->last_event = U_BAM_CONNECT_E;
	queue_work(gbam_wq, &port->connect_w);

	ret = 0;
exit:
	spin_unlock_irqrestore(&port->port_lock, flags);
	return ret;
}

int gbam_setup(enum bam_dmux_func_type func)
{
	int	ret;

	pr_debug("%s: requested BAM port:%d\n", __func__, func);

	if (func >= BAM_DMUX_NUM_FUNCS) {
		pr_err("%s: Invalid num of ports count:%d\n", __func__, func);
		return -EINVAL;
	}

	if (!gbam_wq) {
		gbam_wq = alloc_workqueue("k_gbam", WQ_UNBOUND |
					WQ_MEM_RECLAIM, 1);
		if (!gbam_wq) {
			pr_err("%s: Unable to create workqueue gbam_wq\n",
					__func__);
			return -ENOMEM;
		}
	}

	ret = gbam_port_alloc(func);
	if (ret) {
		pr_err("%s: Unable to alloc port:%d\n", __func__, func);
		goto destroy_wq;
	}

	gbam_debugfs_init();

	return 0;

destroy_wq:
	destroy_workqueue(gbam_wq);

	return ret;
}

void gbam_cleanup(enum bam_dmux_func_type func)
{
	gbam_debugfs_remove();
	flush_workqueue(gbam_wq);
	gbam_port_free(func);
}

int gbam_mbim_connect(struct usb_gadget *g, struct usb_ep *in,
			struct usb_ep *out)
{
	struct data_port *gr;

	gr = kzalloc(sizeof(*gr), GFP_ATOMIC);
	if (!gr)
		return -ENOMEM;
	gr->in = in;
	gr->out = out;
	gr->cdev->gadget = g;

	return gbam_connect(gr, BAM_DMUX_FUNC_MBIM);
}

void gbam_mbim_disconnect(void)
{
	struct gbam_port *port = bam_ports[BAM_DMUX_FUNC_MBIM].port;
	struct data_port *gr = port->port_usb;

	if (!gr) {
		pr_err("%s: port_usb is NULL\n", __func__);
		return;
	}

	gbam_disconnect(gr, BAM_DMUX_FUNC_MBIM);
	kfree(gr);
}

int gbam_mbim_setup(void)
{
	int ret = 0;

	if (!bam_ports[BAM_DMUX_FUNC_RMNET].port)
		ret = gbam_setup(BAM_DMUX_FUNC_MBIM);

	return ret;
}
