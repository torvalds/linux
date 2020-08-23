// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#include <linux/module.h>
#include "mt76.h"
#include "usb_trace.h"
#include "dma.h"

#define MT_VEND_REQ_MAX_RETRY	10
#define MT_VEND_REQ_TOUT_MS	300

static bool disable_usb_sg;
module_param_named(disable_usb_sg, disable_usb_sg, bool, 0644);
MODULE_PARM_DESC(disable_usb_sg, "Disable usb scatter-gather support");

static int __mt76u_vendor_request(struct mt76_dev *dev, u8 req,
				  u8 req_type, u16 val, u16 offset,
				  void *buf, size_t len)
{
	struct usb_interface *uintf = to_usb_interface(dev->dev);
	struct usb_device *udev = interface_to_usbdev(uintf);
	unsigned int pipe;
	int i, ret;

	lockdep_assert_held(&dev->usb.usb_ctrl_mtx);

	pipe = (req_type & USB_DIR_IN) ? usb_rcvctrlpipe(udev, 0)
				       : usb_sndctrlpipe(udev, 0);
	for (i = 0; i < MT_VEND_REQ_MAX_RETRY; i++) {
		if (test_bit(MT76_REMOVED, &dev->phy.state))
			return -EIO;

		ret = usb_control_msg(udev, pipe, req, req_type, val,
				      offset, buf, len, MT_VEND_REQ_TOUT_MS);
		if (ret == -ENODEV)
			set_bit(MT76_REMOVED, &dev->phy.state);
		if (ret >= 0 || ret == -ENODEV)
			return ret;
		usleep_range(5000, 10000);
	}

	dev_err(dev->dev, "vendor request req:%02x off:%04x failed:%d\n",
		req, offset, ret);
	return ret;
}

int mt76u_vendor_request(struct mt76_dev *dev, u8 req,
			 u8 req_type, u16 val, u16 offset,
			 void *buf, size_t len)
{
	int ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = __mt76u_vendor_request(dev, req, req_type,
				     val, offset, buf, len);
	trace_usb_reg_wr(dev, offset, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76u_vendor_request);

static u32 ___mt76u_rr(struct mt76_dev *dev, u8 req, u32 addr)
{
	struct mt76_usb *usb = &dev->usb;
	u32 data = ~0;
	int ret;

	ret = __mt76u_vendor_request(dev, req,
				     USB_DIR_IN | USB_TYPE_VENDOR,
				     addr >> 16, addr, usb->data,
				     sizeof(__le32));
	if (ret == sizeof(__le32))
		data = get_unaligned_le32(usb->data);
	trace_usb_reg_rr(dev, addr, data);

	return data;
}

static u32 __mt76u_rr(struct mt76_dev *dev, u32 addr)
{
	u8 req;

	switch (addr & MT_VEND_TYPE_MASK) {
	case MT_VEND_TYPE_EEPROM:
		req = MT_VEND_READ_EEPROM;
		break;
	case MT_VEND_TYPE_CFG:
		req = MT_VEND_READ_CFG;
		break;
	default:
		req = MT_VEND_MULTI_READ;
		break;
	}

	return ___mt76u_rr(dev, req, addr & ~MT_VEND_TYPE_MASK);
}

static u32 mt76u_rr(struct mt76_dev *dev, u32 addr)
{
	u32 ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = __mt76u_rr(dev, addr);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}

static u32 mt76u_rr_ext(struct mt76_dev *dev, u32 addr)
{
	u32 ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = ___mt76u_rr(dev, MT_VEND_READ_EXT, addr);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}

static void ___mt76u_wr(struct mt76_dev *dev, u8 req,
			u32 addr, u32 val)
{
	struct mt76_usb *usb = &dev->usb;

	put_unaligned_le32(val, usb->data);
	__mt76u_vendor_request(dev, req,
			       USB_DIR_OUT | USB_TYPE_VENDOR,
			       addr >> 16, addr, usb->data,
			       sizeof(__le32));
	trace_usb_reg_wr(dev, addr, val);
}

static void __mt76u_wr(struct mt76_dev *dev, u32 addr, u32 val)
{
	u8 req;

	switch (addr & MT_VEND_TYPE_MASK) {
	case MT_VEND_TYPE_CFG:
		req = MT_VEND_WRITE_CFG;
		break;
	default:
		req = MT_VEND_MULTI_WRITE;
		break;
	}
	___mt76u_wr(dev, req, addr & ~MT_VEND_TYPE_MASK, val);
}

static void mt76u_wr(struct mt76_dev *dev, u32 addr, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	__mt76u_wr(dev, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}

static void mt76u_wr_ext(struct mt76_dev *dev, u32 addr, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	___mt76u_wr(dev, MT_VEND_WRITE_EXT, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}

static u32 mt76u_rmw(struct mt76_dev *dev, u32 addr,
		     u32 mask, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	val |= __mt76u_rr(dev, addr) & ~mask;
	__mt76u_wr(dev, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return val;
}

static u32 mt76u_rmw_ext(struct mt76_dev *dev, u32 addr,
			 u32 mask, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	val |= ___mt76u_rr(dev, MT_VEND_READ_EXT, addr) & ~mask;
	___mt76u_wr(dev, MT_VEND_WRITE_EXT, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return val;
}

static void mt76u_copy(struct mt76_dev *dev, u32 offset,
		       const void *data, int len)
{
	struct mt76_usb *usb = &dev->usb;
	const u8 *val = data;
	int ret;
	int current_batch_size;
	int i = 0;

	/* Assure that always a multiple of 4 bytes are copied,
	 * otherwise beacons can be corrupted.
	 * See: "mt76: round up length on mt76_wr_copy"
	 * Commit 850e8f6fbd5d0003b0
	 */
	len = round_up(len, 4);

	mutex_lock(&usb->usb_ctrl_mtx);
	while (i < len) {
		current_batch_size = min_t(int, usb->data_len, len - i);
		memcpy(usb->data, val + i, current_batch_size);
		ret = __mt76u_vendor_request(dev, MT_VEND_MULTI_WRITE,
					     USB_DIR_OUT | USB_TYPE_VENDOR,
					     0, offset + i, usb->data,
					     current_batch_size);
		if (ret < 0)
			break;

		i += current_batch_size;
	}
	mutex_unlock(&usb->usb_ctrl_mtx);
}

static void mt76u_copy_ext(struct mt76_dev *dev, u32 offset,
			   const void *data, int len)
{
	struct mt76_usb *usb = &dev->usb;
	int ret, i = 0, batch_len;
	const u8 *val = data;

	len = round_up(len, 4);
	mutex_lock(&usb->usb_ctrl_mtx);
	while (i < len) {
		batch_len = min_t(int, usb->data_len, len - i);
		memcpy(usb->data, val + i, batch_len);
		ret = __mt76u_vendor_request(dev, MT_VEND_WRITE_EXT,
					     USB_DIR_OUT | USB_TYPE_VENDOR,
					     (offset + i) >> 16, offset + i,
					     usb->data, batch_len);
		if (ret < 0)
			break;

		i += batch_len;
	}
	mutex_unlock(&usb->usb_ctrl_mtx);
}

static void
mt76u_read_copy_ext(struct mt76_dev *dev, u32 offset,
		    void *data, int len)
{
	struct mt76_usb *usb = &dev->usb;
	int i = 0, batch_len, ret;
	u8 *val = data;

	len = round_up(len, 4);
	mutex_lock(&usb->usb_ctrl_mtx);
	while (i < len) {
		batch_len = min_t(int, usb->data_len, len - i);
		ret = __mt76u_vendor_request(dev, MT_VEND_READ_EXT,
					     USB_DIR_IN | USB_TYPE_VENDOR,
					     (offset + i) >> 16, offset + i,
					     usb->data, batch_len);
		if (ret < 0)
			break;

		memcpy(val + i, usb->data, batch_len);
		i += batch_len;
	}
	mutex_unlock(&usb->usb_ctrl_mtx);
}

void mt76u_single_wr(struct mt76_dev *dev, const u8 req,
		     const u16 offset, const u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	__mt76u_vendor_request(dev, req,
			       USB_DIR_OUT | USB_TYPE_VENDOR,
			       val & 0xffff, offset, NULL, 0);
	__mt76u_vendor_request(dev, req,
			       USB_DIR_OUT | USB_TYPE_VENDOR,
			       val >> 16, offset + 2, NULL, 0);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}
EXPORT_SYMBOL_GPL(mt76u_single_wr);

static int
mt76u_req_wr_rp(struct mt76_dev *dev, u32 base,
		const struct mt76_reg_pair *data, int len)
{
	struct mt76_usb *usb = &dev->usb;

	mutex_lock(&usb->usb_ctrl_mtx);
	while (len > 0) {
		__mt76u_wr(dev, base + data->reg, data->value);
		len--;
		data++;
	}
	mutex_unlock(&usb->usb_ctrl_mtx);

	return 0;
}

static int
mt76u_wr_rp(struct mt76_dev *dev, u32 base,
	    const struct mt76_reg_pair *data, int n)
{
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->phy.state))
		return dev->mcu_ops->mcu_wr_rp(dev, base, data, n);
	else
		return mt76u_req_wr_rp(dev, base, data, n);
}

static int
mt76u_req_rd_rp(struct mt76_dev *dev, u32 base, struct mt76_reg_pair *data,
		int len)
{
	struct mt76_usb *usb = &dev->usb;

	mutex_lock(&usb->usb_ctrl_mtx);
	while (len > 0) {
		data->value = __mt76u_rr(dev, base + data->reg);
		len--;
		data++;
	}
	mutex_unlock(&usb->usb_ctrl_mtx);

	return 0;
}

static int
mt76u_rd_rp(struct mt76_dev *dev, u32 base,
	    struct mt76_reg_pair *data, int n)
{
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->phy.state))
		return dev->mcu_ops->mcu_rd_rp(dev, base, data, n);
	else
		return mt76u_req_rd_rp(dev, base, data, n);
}

static bool mt76u_check_sg(struct mt76_dev *dev)
{
	struct usb_interface *uintf = to_usb_interface(dev->dev);
	struct usb_device *udev = interface_to_usbdev(uintf);

	return (!disable_usb_sg && udev->bus->sg_tablesize > 0 &&
		(udev->bus->no_sg_constraint ||
		 udev->speed == USB_SPEED_WIRELESS));
}

static int
mt76u_set_endpoints(struct usb_interface *intf,
		    struct mt76_usb *usb)
{
	struct usb_host_interface *intf_desc = intf->cur_altsetting;
	struct usb_endpoint_descriptor *ep_desc;
	int i, in_ep = 0, out_ep = 0;

	for (i = 0; i < intf_desc->desc.bNumEndpoints; i++) {
		ep_desc = &intf_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep_desc) &&
		    in_ep < __MT_EP_IN_MAX) {
			usb->in_ep[in_ep] = usb_endpoint_num(ep_desc);
			in_ep++;
		} else if (usb_endpoint_is_bulk_out(ep_desc) &&
			   out_ep < __MT_EP_OUT_MAX) {
			usb->out_ep[out_ep] = usb_endpoint_num(ep_desc);
			out_ep++;
		}
	}

	if (in_ep != __MT_EP_IN_MAX || out_ep != __MT_EP_OUT_MAX)
		return -EINVAL;
	return 0;
}

static int
mt76u_fill_rx_sg(struct mt76_dev *dev, struct mt76_queue *q, struct urb *urb,
		 int nsgs, gfp_t gfp)
{
	int i;

	for (i = 0; i < nsgs; i++) {
		struct page *page;
		void *data;
		int offset;

		data = page_frag_alloc(&q->rx_page, q->buf_size, gfp);
		if (!data)
			break;

		page = virt_to_head_page(data);
		offset = data - page_address(page);
		sg_set_page(&urb->sg[i], page, q->buf_size, offset);
	}

	if (i < nsgs) {
		int j;

		for (j = nsgs; j < urb->num_sgs; j++)
			skb_free_frag(sg_virt(&urb->sg[j]));
		urb->num_sgs = i;
	}

	urb->num_sgs = max_t(int, i, urb->num_sgs);
	urb->transfer_buffer_length = urb->num_sgs * q->buf_size;
	sg_init_marker(urb->sg, urb->num_sgs);

	return i ? : -ENOMEM;
}

static int
mt76u_refill_rx(struct mt76_dev *dev, struct mt76_queue *q,
		struct urb *urb, int nsgs, gfp_t gfp)
{
	enum mt76_rxq_id qid = q - &dev->q_rx[MT_RXQ_MAIN];

	if (qid == MT_RXQ_MAIN && dev->usb.sg_en)
		return mt76u_fill_rx_sg(dev, q, urb, nsgs, gfp);

	urb->transfer_buffer_length = q->buf_size;
	urb->transfer_buffer = page_frag_alloc(&q->rx_page, q->buf_size, gfp);

	return urb->transfer_buffer ? 0 : -ENOMEM;
}

static int
mt76u_urb_alloc(struct mt76_dev *dev, struct mt76_queue_entry *e,
		int sg_max_size)
{
	unsigned int size = sizeof(struct urb);

	if (dev->usb.sg_en)
		size += sg_max_size * sizeof(struct scatterlist);

	e->urb = kzalloc(size, GFP_KERNEL);
	if (!e->urb)
		return -ENOMEM;

	usb_init_urb(e->urb);

	if (dev->usb.sg_en && sg_max_size > 0)
		e->urb->sg = (struct scatterlist *)(e->urb + 1);

	return 0;
}

static int
mt76u_rx_urb_alloc(struct mt76_dev *dev, struct mt76_queue *q,
		   struct mt76_queue_entry *e)
{
	enum mt76_rxq_id qid = q - &dev->q_rx[MT_RXQ_MAIN];
	int err, sg_size;

	sg_size = qid == MT_RXQ_MAIN ? MT_RX_SG_MAX_SIZE : 0;
	err = mt76u_urb_alloc(dev, e, sg_size);
	if (err)
		return err;

	return mt76u_refill_rx(dev, q, e->urb, sg_size, GFP_KERNEL);
}

static void mt76u_urb_free(struct urb *urb)
{
	int i;

	for (i = 0; i < urb->num_sgs; i++)
		skb_free_frag(sg_virt(&urb->sg[i]));

	if (urb->transfer_buffer)
		skb_free_frag(urb->transfer_buffer);

	usb_free_urb(urb);
}

static void
mt76u_fill_bulk_urb(struct mt76_dev *dev, int dir, int index,
		    struct urb *urb, usb_complete_t complete_fn,
		    void *context)
{
	struct usb_interface *uintf = to_usb_interface(dev->dev);
	struct usb_device *udev = interface_to_usbdev(uintf);
	unsigned int pipe;

	if (dir == USB_DIR_IN)
		pipe = usb_rcvbulkpipe(udev, dev->usb.in_ep[index]);
	else
		pipe = usb_sndbulkpipe(udev, dev->usb.out_ep[index]);

	urb->dev = udev;
	urb->pipe = pipe;
	urb->complete = complete_fn;
	urb->context = context;
}

static struct urb *
mt76u_get_next_rx_entry(struct mt76_queue *q)
{
	struct urb *urb = NULL;
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	if (q->queued > 0) {
		urb = q->entry[q->tail].urb;
		q->tail = (q->tail + 1) % q->ndesc;
		q->queued--;
	}
	spin_unlock_irqrestore(&q->lock, flags);

	return urb;
}

static int
mt76u_get_rx_entry_len(struct mt76_dev *dev, u8 *data,
		       u32 data_len)
{
	u16 dma_len, min_len;

	dma_len = get_unaligned_le16(data);
	if (dev->drv->drv_flags & MT_DRV_RX_DMA_HDR)
		return dma_len;

	min_len = MT_DMA_HDR_LEN + MT_RX_RXWI_LEN + MT_FCE_INFO_LEN;
	if (data_len < min_len || !dma_len ||
	    dma_len + MT_DMA_HDR_LEN > data_len ||
	    (dma_len & 0x3))
		return -EINVAL;
	return dma_len;
}

static struct sk_buff *
mt76u_build_rx_skb(struct mt76_dev *dev, void *data,
		   int len, int buf_size)
{
	int head_room, drv_flags = dev->drv->drv_flags;
	struct sk_buff *skb;

	head_room = drv_flags & MT_DRV_RX_DMA_HDR ? 0 : MT_DMA_HDR_LEN;
	if (SKB_WITH_OVERHEAD(buf_size) < head_room + len) {
		struct page *page;

		/* slow path, not enough space for data and
		 * skb_shared_info
		 */
		skb = alloc_skb(MT_SKB_HEAD_LEN, GFP_ATOMIC);
		if (!skb)
			return NULL;

		skb_put_data(skb, data + head_room, MT_SKB_HEAD_LEN);
		data += head_room + MT_SKB_HEAD_LEN;
		page = virt_to_head_page(data);
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
				page, data - page_address(page),
				len - MT_SKB_HEAD_LEN, buf_size);

		return skb;
	}

	/* fast path */
	skb = build_skb(data, buf_size);
	if (!skb)
		return NULL;

	skb_reserve(skb, head_room);
	__skb_put(skb, len);

	return skb;
}

static int
mt76u_process_rx_entry(struct mt76_dev *dev, struct urb *urb,
		       int buf_size)
{
	u8 *data = urb->num_sgs ? sg_virt(&urb->sg[0]) : urb->transfer_buffer;
	int data_len = urb->num_sgs ? urb->sg[0].length : urb->actual_length;
	int len, nsgs = 1, head_room, drv_flags = dev->drv->drv_flags;
	struct sk_buff *skb;

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->phy.state))
		return 0;

	len = mt76u_get_rx_entry_len(dev, data, urb->actual_length);
	if (len < 0)
		return 0;

	head_room = drv_flags & MT_DRV_RX_DMA_HDR ? 0 : MT_DMA_HDR_LEN;
	data_len = min_t(int, len, data_len - head_room);
	skb = mt76u_build_rx_skb(dev, data, data_len, buf_size);
	if (!skb)
		return 0;

	len -= data_len;
	while (len > 0 && nsgs < urb->num_sgs) {
		data_len = min_t(int, len, urb->sg[nsgs].length);
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
				sg_page(&urb->sg[nsgs]),
				urb->sg[nsgs].offset, data_len,
				buf_size);
		len -= data_len;
		nsgs++;
	}
	dev->drv->rx_skb(dev, MT_RXQ_MAIN, skb);

	return nsgs;
}

static void mt76u_complete_rx(struct urb *urb)
{
	struct mt76_dev *dev = dev_get_drvdata(&urb->dev->dev);
	struct mt76_queue *q = urb->context;
	unsigned long flags;

	trace_rx_urb(dev, urb);

	switch (urb->status) {
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -ENOENT:
		return;
	default:
		dev_err_ratelimited(dev->dev, "rx urb failed: %d\n",
				    urb->status);
		/* fall through */
	case 0:
		break;
	}

	spin_lock_irqsave(&q->lock, flags);
	if (WARN_ONCE(q->entry[q->head].urb != urb, "rx urb mismatch"))
		goto out;

	q->head = (q->head + 1) % q->ndesc;
	q->queued++;
	tasklet_schedule(&dev->usb.rx_tasklet);
out:
	spin_unlock_irqrestore(&q->lock, flags);
}

static int
mt76u_submit_rx_buf(struct mt76_dev *dev, enum mt76_rxq_id qid,
		    struct urb *urb)
{
	int ep = qid == MT_RXQ_MAIN ? MT_EP_IN_PKT_RX : MT_EP_IN_CMD_RESP;

	mt76u_fill_bulk_urb(dev, USB_DIR_IN, ep, urb,
			    mt76u_complete_rx, &dev->q_rx[qid]);
	trace_submit_urb(dev, urb);

	return usb_submit_urb(urb, GFP_ATOMIC);
}

static void
mt76u_process_rx_queue(struct mt76_dev *dev, struct mt76_queue *q)
{
	int qid = q - &dev->q_rx[MT_RXQ_MAIN];
	struct urb *urb;
	int err, count;

	while (true) {
		urb = mt76u_get_next_rx_entry(q);
		if (!urb)
			break;

		count = mt76u_process_rx_entry(dev, urb, q->buf_size);
		if (count > 0) {
			err = mt76u_refill_rx(dev, q, urb, count, GFP_ATOMIC);
			if (err < 0)
				break;
		}
		mt76u_submit_rx_buf(dev, qid, urb);
	}
	if (qid == MT_RXQ_MAIN)
		mt76_rx_poll_complete(dev, MT_RXQ_MAIN, NULL);
}

static void mt76u_rx_tasklet(unsigned long data)
{
	struct mt76_dev *dev = (struct mt76_dev *)data;
	int i;

	rcu_read_lock();
	mt76_for_each_q_rx(dev, i)
		mt76u_process_rx_queue(dev, &dev->q_rx[i]);
	rcu_read_unlock();
}

static int
mt76u_submit_rx_buffers(struct mt76_dev *dev, enum mt76_rxq_id qid)
{
	struct mt76_queue *q = &dev->q_rx[qid];
	unsigned long flags;
	int i, err = 0;

	spin_lock_irqsave(&q->lock, flags);
	for (i = 0; i < q->ndesc; i++) {
		err = mt76u_submit_rx_buf(dev, qid, q->entry[i].urb);
		if (err < 0)
			break;
	}
	q->head = q->tail = 0;
	q->queued = 0;
	spin_unlock_irqrestore(&q->lock, flags);

	return err;
}

static int
mt76u_alloc_rx_queue(struct mt76_dev *dev, enum mt76_rxq_id qid)
{
	struct mt76_queue *q = &dev->q_rx[qid];
	int i, err;

	spin_lock_init(&q->lock);
	q->entry = devm_kcalloc(dev->dev,
				MT_NUM_RX_ENTRIES, sizeof(*q->entry),
				GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	q->ndesc = MT_NUM_RX_ENTRIES;
	q->buf_size = PAGE_SIZE;

	for (i = 0; i < q->ndesc; i++) {
		err = mt76u_rx_urb_alloc(dev, q, &q->entry[i]);
		if (err < 0)
			return err;
	}

	return mt76u_submit_rx_buffers(dev, qid);
}

int mt76u_alloc_mcu_queue(struct mt76_dev *dev)
{
	return mt76u_alloc_rx_queue(dev, MT_RXQ_MCU);
}
EXPORT_SYMBOL_GPL(mt76u_alloc_mcu_queue);

static void
mt76u_free_rx_queue(struct mt76_dev *dev, struct mt76_queue *q)
{
	struct page *page;
	int i;

	for (i = 0; i < q->ndesc; i++)
		mt76u_urb_free(q->entry[i].urb);

	if (!q->rx_page.va)
		return;

	page = virt_to_page(q->rx_page.va);
	__page_frag_cache_drain(page, q->rx_page.pagecnt_bias);
	memset(&q->rx_page, 0, sizeof(q->rx_page));
}

static void mt76u_free_rx(struct mt76_dev *dev)
{
	int i;

	mt76_for_each_q_rx(dev, i)
		mt76u_free_rx_queue(dev, &dev->q_rx[i]);
}

void mt76u_stop_rx(struct mt76_dev *dev)
{
	int i;

	mt76_for_each_q_rx(dev, i) {
		struct mt76_queue *q = &dev->q_rx[i];
		int j;

		for (j = 0; j < q->ndesc; j++)
			usb_poison_urb(q->entry[j].urb);
	}

	tasklet_kill(&dev->usb.rx_tasklet);
}
EXPORT_SYMBOL_GPL(mt76u_stop_rx);

int mt76u_resume_rx(struct mt76_dev *dev)
{
	int i;

	mt76_for_each_q_rx(dev, i) {
		struct mt76_queue *q = &dev->q_rx[i];
		int err, j;

		for (j = 0; j < q->ndesc; j++)
			usb_unpoison_urb(q->entry[j].urb);

		err = mt76u_submit_rx_buffers(dev, i);
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76u_resume_rx);

static void mt76u_tx_tasklet(unsigned long data)
{
	struct mt76_dev *dev = (struct mt76_dev *)data;
	struct mt76_queue_entry entry;
	struct mt76_sw_queue *sq;
	struct mt76_queue *q;
	bool wake;
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		sq = &dev->q_tx[i];
		q = sq->q;

		while (q->queued > 0) {
			if (!q->entry[q->tail].done)
				break;

			entry = q->entry[q->tail];
			q->entry[q->tail].done = false;

			mt76_queue_tx_complete(dev, q, &entry);
		}

		wake = q->stopped && q->queued < q->ndesc - 8;
		if (wake)
			q->stopped = false;

		if (!q->queued)
			wake_up(&dev->tx_wait);

		mt76_txq_schedule(&dev->phy, i);

		if (dev->drv->tx_status_data &&
		    !test_and_set_bit(MT76_READING_STATS, &dev->phy.state))
			queue_work(dev->wq, &dev->usb.stat_work);
		if (wake)
			ieee80211_wake_queue(dev->hw, i);
	}
}

static void mt76u_tx_status_data(struct work_struct *work)
{
	struct mt76_usb *usb;
	struct mt76_dev *dev;
	u8 update = 1;
	u16 count = 0;

	usb = container_of(work, struct mt76_usb, stat_work);
	dev = container_of(usb, struct mt76_dev, usb);

	while (true) {
		if (test_bit(MT76_REMOVED, &dev->phy.state))
			break;

		if (!dev->drv->tx_status_data(dev, &update))
			break;
		count++;
	}

	if (count && test_bit(MT76_STATE_RUNNING, &dev->phy.state))
		queue_work(dev->wq, &usb->stat_work);
	else
		clear_bit(MT76_READING_STATS, &dev->phy.state);
}

static void mt76u_complete_tx(struct urb *urb)
{
	struct mt76_dev *dev = dev_get_drvdata(&urb->dev->dev);
	struct mt76_queue_entry *e = urb->context;

	if (mt76u_urb_error(urb))
		dev_err(dev->dev, "tx urb failed: %d\n", urb->status);
	e->done = true;

	tasklet_schedule(&dev->tx_tasklet);
}

static int
mt76u_tx_setup_buffers(struct mt76_dev *dev, struct sk_buff *skb,
		       struct urb *urb)
{
	urb->transfer_buffer_length = skb->len;

	if (!dev->usb.sg_en) {
		urb->transfer_buffer = skb->data;
		return 0;
	}

	sg_init_table(urb->sg, MT_TX_SG_MAX_SIZE);
	urb->num_sgs = skb_to_sgvec(skb, urb->sg, 0, skb->len);
	if (!urb->num_sgs)
		return -ENOMEM;

	return urb->num_sgs;
}

static int
mt76u_tx_queue_skb(struct mt76_dev *dev, enum mt76_txq_id qid,
		   struct sk_buff *skb, struct mt76_wcid *wcid,
		   struct ieee80211_sta *sta)
{
	struct mt76_queue *q = dev->q_tx[qid].q;
	struct mt76_tx_info tx_info = {
		.skb = skb,
	};
	u16 idx = q->head;
	int err;

	if (q->queued == q->ndesc)
		return -ENOSPC;

	skb->prev = skb->next = NULL;
	err = dev->drv->tx_prepare_skb(dev, NULL, qid, wcid, sta, &tx_info);
	if (err < 0)
		return err;

	err = mt76u_tx_setup_buffers(dev, tx_info.skb, q->entry[idx].urb);
	if (err < 0)
		return err;

	mt76u_fill_bulk_urb(dev, USB_DIR_OUT, q2ep(q->hw_idx),
			    q->entry[idx].urb, mt76u_complete_tx,
			    &q->entry[idx]);

	q->head = (q->head + 1) % q->ndesc;
	q->entry[idx].skb = tx_info.skb;
	q->queued++;

	return idx;
}

static void mt76u_tx_kick(struct mt76_dev *dev, struct mt76_queue *q)
{
	struct urb *urb;
	int err;

	while (q->first != q->head) {
		urb = q->entry[q->first].urb;

		trace_submit_urb(dev, urb);
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0) {
			if (err == -ENODEV)
				set_bit(MT76_REMOVED, &dev->phy.state);
			else
				dev_err(dev->dev, "tx urb submit failed:%d\n",
					err);
			break;
		}
		q->first = (q->first + 1) % q->ndesc;
	}
}

static u8 mt76u_ac_to_hwq(struct mt76_dev *dev, u8 ac)
{
	if (mt76_chip(dev) == 0x7663) {
		static const u8 lmac_queue_map[] = {
			/* ac to lmac mapping */
			[IEEE80211_AC_BK] = 0,
			[IEEE80211_AC_BE] = 1,
			[IEEE80211_AC_VI] = 2,
			[IEEE80211_AC_VO] = 4,
		};

		if (WARN_ON(ac >= ARRAY_SIZE(lmac_queue_map)))
			return 1; /* BE */

		return lmac_queue_map[ac];
	}

	return mt76_ac_to_hwq(ac);
}

static int mt76u_alloc_tx(struct mt76_dev *dev)
{
	struct mt76_queue *q;
	int i, j, err;

	for (i = 0; i <= MT_TXQ_PSD; i++) {
		if (i >= IEEE80211_NUM_ACS) {
			dev->q_tx[i].q = dev->q_tx[0].q;
			continue;
		}

		q = devm_kzalloc(dev->dev, sizeof(*q), GFP_KERNEL);
		if (!q)
			return -ENOMEM;

		spin_lock_init(&q->lock);
		q->hw_idx = mt76u_ac_to_hwq(dev, i);
		dev->q_tx[i].q = q;

		q->entry = devm_kcalloc(dev->dev,
					MT_NUM_TX_ENTRIES, sizeof(*q->entry),
					GFP_KERNEL);
		if (!q->entry)
			return -ENOMEM;

		q->ndesc = MT_NUM_TX_ENTRIES;
		for (j = 0; j < q->ndesc; j++) {
			err = mt76u_urb_alloc(dev, &q->entry[j],
					      MT_TX_SG_MAX_SIZE);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static void mt76u_free_tx(struct mt76_dev *dev)
{
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		struct mt76_queue *q;
		int j;

		q = dev->q_tx[i].q;
		if (!q)
			continue;

		for (j = 0; j < q->ndesc; j++)
			usb_free_urb(q->entry[j].urb);
	}
}

void mt76u_stop_tx(struct mt76_dev *dev)
{
	int ret;

	ret = wait_event_timeout(dev->tx_wait, !mt76_has_tx_pending(&dev->phy),
				 HZ / 5);
	if (!ret) {
		struct mt76_queue_entry entry;
		struct mt76_queue *q;
		int i, j;

		dev_err(dev->dev, "timed out waiting for pending tx\n");

		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			q = dev->q_tx[i].q;
			if (!q)
				continue;

			for (j = 0; j < q->ndesc; j++)
				usb_kill_urb(q->entry[j].urb);
		}

		tasklet_kill(&dev->tx_tasklet);

		/* On device removal we maight queue skb's, but mt76u_tx_kick()
		 * will fail to submit urb, cleanup those skb's manually.
		 */
		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			q = dev->q_tx[i].q;
			if (!q)
				continue;

			entry = q->entry[q->tail];
			q->entry[q->tail].done = false;

			mt76_queue_tx_complete(dev, q, &entry);
		}
	}

	cancel_work_sync(&dev->usb.stat_work);
	clear_bit(MT76_READING_STATS, &dev->phy.state);

	mt76_tx_status_check(dev, NULL, true);
}
EXPORT_SYMBOL_GPL(mt76u_stop_tx);

void mt76u_queues_deinit(struct mt76_dev *dev)
{
	mt76u_stop_rx(dev);
	mt76u_stop_tx(dev);

	mt76u_free_rx(dev);
	mt76u_free_tx(dev);
}
EXPORT_SYMBOL_GPL(mt76u_queues_deinit);

int mt76u_alloc_queues(struct mt76_dev *dev)
{
	int err;

	err = mt76u_alloc_rx_queue(dev, MT_RXQ_MAIN);
	if (err < 0)
		return err;

	return mt76u_alloc_tx(dev);
}
EXPORT_SYMBOL_GPL(mt76u_alloc_queues);

static const struct mt76_queue_ops usb_queue_ops = {
	.tx_queue_skb = mt76u_tx_queue_skb,
	.kick = mt76u_tx_kick,
};

int mt76u_init(struct mt76_dev *dev,
	       struct usb_interface *intf, bool ext)
{
	static struct mt76_bus_ops mt76u_ops = {
		.read_copy = mt76u_read_copy_ext,
		.wr_rp = mt76u_wr_rp,
		.rd_rp = mt76u_rd_rp,
		.type = MT76_BUS_USB,
	};
	struct usb_device *udev = interface_to_usbdev(intf);
	struct mt76_usb *usb = &dev->usb;
	int err = -ENOMEM;

	mt76u_ops.rr = ext ? mt76u_rr_ext : mt76u_rr;
	mt76u_ops.wr = ext ? mt76u_wr_ext : mt76u_wr;
	mt76u_ops.rmw = ext ? mt76u_rmw_ext : mt76u_rmw;
	mt76u_ops.write_copy = ext ? mt76u_copy_ext : mt76u_copy;

	tasklet_init(&usb->rx_tasklet, mt76u_rx_tasklet, (unsigned long)dev);
	tasklet_init(&dev->tx_tasklet, mt76u_tx_tasklet, (unsigned long)dev);
	INIT_WORK(&usb->stat_work, mt76u_tx_status_data);

	usb->data_len = usb_maxpacket(udev, usb_sndctrlpipe(udev, 0), 1);
	if (usb->data_len < 32)
		usb->data_len = 32;

	usb->data = devm_kmalloc(dev->dev, usb->data_len, GFP_KERNEL);
	if (!usb->data)
		goto error;

	mutex_init(&usb->usb_ctrl_mtx);
	dev->bus = &mt76u_ops;
	dev->queue_ops = &usb_queue_ops;

	dev_set_drvdata(&udev->dev, dev);

	usb->sg_en = mt76u_check_sg(dev);

	err = mt76u_set_endpoints(intf, usb);
	if (err < 0)
		goto error;

	return 0;

error:
	destroy_workqueue(dev->wq);

	return err;
}
EXPORT_SYMBOL_GPL(mt76u_init);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>");
MODULE_LICENSE("Dual BSD/GPL");
