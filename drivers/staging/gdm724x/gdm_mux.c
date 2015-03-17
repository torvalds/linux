/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/usb/cdc.h>

#include "gdm_mux.h"

static struct workqueue_struct *mux_rx_wq;

static u16 packet_type[TTY_MAX_COUNT] = {0xF011, 0xF010};

#define USB_DEVICE_CDC_DATA(vid, pid) \
	.match_flags = \
		USB_DEVICE_ID_MATCH_DEVICE |\
		USB_DEVICE_ID_MATCH_INT_CLASS |\
		USB_DEVICE_ID_MATCH_INT_SUBCLASS,\
	.idVendor = vid,\
	.idProduct = pid,\
	.bInterfaceClass = USB_CLASS_COMM,\
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE_CDC_DATA(0x1076, 0x8000) }, /* GCT GDM7240 */
	{ USB_DEVICE_CDC_DATA(0x1076, 0x8f00) }, /* GCT GDM7243 */
	{ USB_DEVICE_CDC_DATA(0x1076, 0x9000) }, /* GCT GDM7243 */
	{ USB_DEVICE_CDC_DATA(0x1d74, 0x2300) }, /* LGIT Phoenix */
	{}
};


MODULE_DEVICE_TABLE(usb, id_table);

static int packet_type_to_index(u16 packetType)
{
	int i;

	for (i = 0; i < TTY_MAX_COUNT; i++) {
		if (packet_type[i] == packetType)
			return i;
	}

	return -1;
}

static struct mux_tx *alloc_mux_tx(int len)
{
	struct mux_tx *t = NULL;

	t = kzalloc(sizeof(struct mux_tx), GFP_ATOMIC);
	if (!t)
		return NULL;

	t->urb = usb_alloc_urb(0, GFP_ATOMIC);
	t->buf = kmalloc(MUX_TX_MAX_SIZE, GFP_ATOMIC);
	if (!t->urb || !t->buf) {
		usb_free_urb(t->urb);
		kfree(t->buf);
		kfree(t);
		return NULL;
	}

	return t;
}

static void free_mux_tx(struct mux_tx *t)
{
	if (t) {
		usb_free_urb(t->urb);
		kfree(t->buf);
		kfree(t);
	}
}

static struct mux_rx *alloc_mux_rx(void)
{
	struct mux_rx *r = NULL;

	r = kzalloc(sizeof(struct mux_rx), GFP_KERNEL);
	if (!r)
		return NULL;

	r->urb = usb_alloc_urb(0, GFP_KERNEL);
	r->buf = kmalloc(MUX_RX_MAX_SIZE, GFP_KERNEL);
	if (!r->urb || !r->buf) {
		usb_free_urb(r->urb);
		kfree(r->buf);
		kfree(r);
		return NULL;
	}

	return r;
}

static void free_mux_rx(struct mux_rx *r)
{
	if (r) {
		usb_free_urb(r->urb);
		kfree(r->buf);
		kfree(r);
	}
}

static struct mux_rx *get_rx_struct(struct rx_cxt *rx)
{
	struct mux_rx *r;
	unsigned long flags;

	spin_lock_irqsave(&rx->free_list_lock, flags);

	if (list_empty(&rx->rx_free_list)) {
		spin_unlock_irqrestore(&rx->free_list_lock, flags);
		return NULL;
	}

	r = list_entry(rx->rx_free_list.prev, struct mux_rx, free_list);
	list_del(&r->free_list);

	spin_unlock_irqrestore(&rx->free_list_lock, flags);

	return r;
}

static void put_rx_struct(struct rx_cxt *rx, struct mux_rx *r)
{
	unsigned long flags;

	spin_lock_irqsave(&rx->free_list_lock, flags);
	list_add_tail(&r->free_list, &rx->rx_free_list);
	spin_unlock_irqrestore(&rx->free_list_lock, flags);
}


static int up_to_host(struct mux_rx *r)
{
	struct mux_dev *mux_dev = (struct mux_dev *)r->mux_dev;
	struct mux_pkt_header *mux_header;
	unsigned int start_flag;
	unsigned int payload_size;
	unsigned short packet_type;
	int dummy_cnt;
	u32 packet_size_sum = r->offset;
	int index;
	int ret = TO_HOST_INVALID_PACKET;
	int len = r->len;

	while (1) {
		mux_header = (struct mux_pkt_header *)(r->buf +
						       packet_size_sum);
		start_flag = __le32_to_cpu(mux_header->start_flag);
		payload_size = __le32_to_cpu(mux_header->payload_size);
		packet_type = __le16_to_cpu(mux_header->packet_type);

		if (start_flag != START_FLAG) {
			pr_err("invalid START_FLAG %x\n", start_flag);
			break;
		}

		dummy_cnt = ALIGN(MUX_HEADER_SIZE + payload_size, 4);

		if (len - packet_size_sum <
			MUX_HEADER_SIZE + payload_size + dummy_cnt) {
			pr_err("invalid payload : %d %d %04x\n",
			       payload_size, len, packet_type);
			break;
		}

		index = packet_type_to_index(packet_type);
		if (index < 0) {
			pr_err("invalid index %d\n", index);
			break;
		}

		ret = r->callback(mux_header->data,
				payload_size,
				index,
				mux_dev->tty_dev,
				RECV_PACKET_PROCESS_CONTINUE
				);
		if (ret == TO_HOST_BUFFER_REQUEST_FAIL) {
			r->offset += packet_size_sum;
			break;
		}

		packet_size_sum += MUX_HEADER_SIZE + payload_size + dummy_cnt;
		if (len - packet_size_sum <= MUX_HEADER_SIZE + 2) {
			ret = r->callback(NULL,
					0,
					index,
					mux_dev->tty_dev,
					RECV_PACKET_PROCESS_COMPLETE
					);
			break;
		}
	}

	return ret;
}

static void do_rx(struct work_struct *work)
{
	struct mux_dev *mux_dev =
		container_of(work, struct mux_dev, work_rx.work);
	struct mux_rx *r;
	struct rx_cxt *rx = (struct rx_cxt *)&mux_dev->rx;
	unsigned long flags;
	int ret = 0;

	while (1) {
		spin_lock_irqsave(&rx->to_host_lock, flags);
		if (list_empty(&rx->to_host_list)) {
			spin_unlock_irqrestore(&rx->to_host_lock, flags);
			break;
		}
		r = list_entry(rx->to_host_list.next, struct mux_rx,
			       to_host_list);
		list_del(&r->to_host_list);
		spin_unlock_irqrestore(&rx->to_host_lock, flags);

		ret = up_to_host(r);
		if (ret == TO_HOST_BUFFER_REQUEST_FAIL)
			pr_err("failed to send mux data to host\n");
		else
			put_rx_struct(rx, r);
	}
}

static void remove_rx_submit_list(struct mux_rx *r, struct rx_cxt *rx)
{
	unsigned long flags;
	struct mux_rx	*r_remove, *r_remove_next;

	spin_lock_irqsave(&rx->submit_list_lock, flags);
	list_for_each_entry_safe(r_remove, r_remove_next, &rx->rx_submit_list,
				 rx_submit_list) {
		if (r == r_remove)
			list_del(&r->rx_submit_list);
	}
	spin_unlock_irqrestore(&rx->submit_list_lock, flags);
}

static void gdm_mux_rcv_complete(struct urb *urb)
{
	struct mux_rx *r = urb->context;
	struct mux_dev *mux_dev = (struct mux_dev *)r->mux_dev;
	struct rx_cxt *rx = &mux_dev->rx;
	unsigned long flags;

	remove_rx_submit_list(r, rx);

	if (urb->status) {
		if (mux_dev->usb_state == PM_NORMAL)
			pr_err("%s: urb status error %d\n",
			       __func__, urb->status);
		put_rx_struct(rx, r);
	} else {
		r->len = r->urb->actual_length;
		spin_lock_irqsave(&rx->to_host_lock, flags);
		list_add_tail(&r->to_host_list, &rx->to_host_list);
		queue_work(mux_rx_wq, &mux_dev->work_rx.work);
		spin_unlock_irqrestore(&rx->to_host_lock, flags);
	}
}

static int gdm_mux_recv(void *priv_dev, int (*cb)(void *data, int len,
			int tty_index, struct tty_dev *tty_dev, int complete))
{
	struct mux_dev *mux_dev = priv_dev;
	struct usb_device *usbdev = mux_dev->usbdev;
	struct mux_rx *r;
	struct rx_cxt *rx = &mux_dev->rx;
	unsigned long flags;
	int ret;

	if (!usbdev) {
		pr_err("device is disconnected\n");
		return -ENODEV;
	}

	r = get_rx_struct(rx);
	if (!r) {
		pr_err("get_rx_struct fail\n");
		return -ENOMEM;
	}

	r->offset = 0;
	r->mux_dev = (void *)mux_dev;
	r->callback = cb;
	mux_dev->rx_cb = cb;

	usb_fill_bulk_urb(r->urb,
			  usbdev,
			  usb_rcvbulkpipe(usbdev, 0x86),
			  r->buf,
			  MUX_RX_MAX_SIZE,
			  gdm_mux_rcv_complete,
			  r);

	spin_lock_irqsave(&rx->submit_list_lock, flags);
	list_add_tail(&r->rx_submit_list, &rx->rx_submit_list);
	spin_unlock_irqrestore(&rx->submit_list_lock, flags);

	ret = usb_submit_urb(r->urb, GFP_KERNEL);

	if (ret) {
		spin_lock_irqsave(&rx->submit_list_lock, flags);
		list_del(&r->rx_submit_list);
		spin_unlock_irqrestore(&rx->submit_list_lock, flags);

		put_rx_struct(rx, r);

		pr_err("usb_submit_urb ret=%d\n", ret);
	}

	usb_mark_last_busy(usbdev);

	return ret;
}

static void gdm_mux_send_complete(struct urb *urb)
{
	struct mux_tx *t = urb->context;

	if (urb->status == -ECONNRESET) {
		pr_info("CONNRESET\n");
		free_mux_tx(t);
		return;
	}

	if (t->callback)
		t->callback(t->cb_data);

	free_mux_tx(t);
}

static int gdm_mux_send(void *priv_dev, void *data, int len, int tty_index,
			void (*cb)(void *data), void *cb_data)
{
	struct mux_dev *mux_dev = priv_dev;
	struct usb_device *usbdev = mux_dev->usbdev;
	struct mux_pkt_header *mux_header;
	struct mux_tx *t = NULL;
	static u32 seq_num = 1;
	int dummy_cnt;
	int total_len;
	int ret;
	unsigned long flags;

	if (mux_dev->usb_state == PM_SUSPEND) {
		ret = usb_autopm_get_interface(mux_dev->intf);
		if (!ret)
			usb_autopm_put_interface(mux_dev->intf);
	}

	spin_lock_irqsave(&mux_dev->write_lock, flags);

	dummy_cnt = ALIGN(MUX_HEADER_SIZE + len, 4);

	total_len = len + MUX_HEADER_SIZE + dummy_cnt;

	t = alloc_mux_tx(total_len);
	if (!t) {
		pr_err("alloc_mux_tx fail\n");
		spin_unlock_irqrestore(&mux_dev->write_lock, flags);
		return -ENOMEM;
	}

	mux_header = (struct mux_pkt_header *)t->buf;
	mux_header->start_flag = __cpu_to_le32(START_FLAG);
	mux_header->seq_num = __cpu_to_le32(seq_num++);
	mux_header->payload_size = __cpu_to_le32((u32)len);
	mux_header->packet_type = __cpu_to_le16(packet_type[tty_index]);

	memcpy(t->buf+MUX_HEADER_SIZE, data, len);
	memset(t->buf+MUX_HEADER_SIZE+len, 0, dummy_cnt);

	t->len = total_len;
	t->callback = cb;
	t->cb_data = cb_data;

	usb_fill_bulk_urb(t->urb,
			  usbdev,
			  usb_sndbulkpipe(usbdev, 5),
			  t->buf,
			  total_len,
			  gdm_mux_send_complete,
			  t);

	ret = usb_submit_urb(t->urb, GFP_ATOMIC);

	spin_unlock_irqrestore(&mux_dev->write_lock, flags);

	if (ret)
		pr_err("usb_submit_urb Error: %d\n", ret);

	usb_mark_last_busy(usbdev);

	return ret;
}

static int gdm_mux_send_control(void *priv_dev, int request, int value,
				void *buf, int len)
{
	struct mux_dev *mux_dev = priv_dev;
	struct usb_device *usbdev = mux_dev->usbdev;
	int ret;

	ret = usb_control_msg(usbdev,
			      usb_sndctrlpipe(usbdev, 0),
			      request,
			      USB_RT_ACM,
			      value,
			      2,
			      buf,
			      len,
			      5000
			     );

	if (ret < 0)
		pr_err("usb_control_msg error: %d\n", ret);

	return ret < 0 ? ret : 0;
}

static void release_usb(struct mux_dev *mux_dev)
{
	struct rx_cxt		*rx = &mux_dev->rx;
	struct mux_rx		*r, *r_next;
	unsigned long		flags;

	cancel_delayed_work(&mux_dev->work_rx);

	spin_lock_irqsave(&rx->submit_list_lock, flags);
	list_for_each_entry_safe(r, r_next, &rx->rx_submit_list,
				 rx_submit_list) {
		spin_unlock_irqrestore(&rx->submit_list_lock, flags);
		usb_kill_urb(r->urb);
		spin_lock_irqsave(&rx->submit_list_lock, flags);
	}
	spin_unlock_irqrestore(&rx->submit_list_lock, flags);

	spin_lock_irqsave(&rx->free_list_lock, flags);
	list_for_each_entry_safe(r, r_next, &rx->rx_free_list, free_list) {
		list_del(&r->free_list);
		free_mux_rx(r);
	}
	spin_unlock_irqrestore(&rx->free_list_lock, flags);

	spin_lock_irqsave(&rx->to_host_lock, flags);
	list_for_each_entry_safe(r, r_next, &rx->to_host_list, to_host_list) {
		if (r->mux_dev == (void *)mux_dev) {
			list_del(&r->to_host_list);
			free_mux_rx(r);
		}
	}
	spin_unlock_irqrestore(&rx->to_host_lock, flags);
}


static int init_usb(struct mux_dev *mux_dev)
{
	struct mux_rx *r;
	struct rx_cxt *rx = &mux_dev->rx;
	int ret = 0;
	int i;

	spin_lock_init(&mux_dev->write_lock);
	INIT_LIST_HEAD(&rx->to_host_list);
	INIT_LIST_HEAD(&rx->rx_submit_list);
	INIT_LIST_HEAD(&rx->rx_free_list);
	spin_lock_init(&rx->to_host_lock);
	spin_lock_init(&rx->submit_list_lock);
	spin_lock_init(&rx->free_list_lock);

	for (i = 0; i < MAX_ISSUE_NUM * 2; i++) {
		r = alloc_mux_rx();
		if (r == NULL) {
			ret = -ENOMEM;
			break;
		}

		list_add(&r->free_list, &rx->rx_free_list);
	}

	INIT_DELAYED_WORK(&mux_dev->work_rx, do_rx);

	return ret;
}

static int gdm_mux_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct mux_dev *mux_dev;
	struct tty_dev *tty_dev;
	u16 idVendor, idProduct;
	int bInterfaceNumber;
	int ret;
	int i;
	struct usb_device *usbdev = interface_to_usbdev(intf);

	bInterfaceNumber = intf->cur_altsetting->desc.bInterfaceNumber;

	idVendor = __le16_to_cpu(usbdev->descriptor.idVendor);
	idProduct = __le16_to_cpu(usbdev->descriptor.idProduct);

	pr_info("mux vid = 0x%04x pid = 0x%04x\n", idVendor, idProduct);

	if (bInterfaceNumber != 2)
		return -ENODEV;

	mux_dev = kzalloc(sizeof(struct mux_dev), GFP_KERNEL);
	if (!mux_dev)
		return -ENOMEM;

	tty_dev = kzalloc(sizeof(struct tty_dev), GFP_KERNEL);
	if (!tty_dev) {
		ret = -ENOMEM;
		goto err_free_mux;
	}

	mux_dev->usbdev = usbdev;
	mux_dev->control_intf = intf;

	ret = init_usb(mux_dev);
	if (ret)
		goto err_free_usb;

	tty_dev->priv_dev = (void *)mux_dev;
	tty_dev->send_func = gdm_mux_send;
	tty_dev->recv_func = gdm_mux_recv;
	tty_dev->send_control = gdm_mux_send_control;

	ret = register_lte_tty_device(tty_dev, &intf->dev);
	if (ret)
		goto err_unregister_tty;

	for (i = 0; i < TTY_MAX_COUNT; i++)
		mux_dev->tty_dev = tty_dev;

	mux_dev->intf = intf;
	mux_dev->usb_state = PM_NORMAL;

	usb_get_dev(usbdev);
	usb_set_intfdata(intf, tty_dev);

	return 0;

err_unregister_tty:
	unregister_lte_tty_device(tty_dev);
err_free_usb:
	release_usb(mux_dev);
	kfree(tty_dev);
err_free_mux:
	kfree(mux_dev);

	return ret;
}

static void gdm_mux_disconnect(struct usb_interface *intf)
{
	struct tty_dev *tty_dev;
	struct mux_dev *mux_dev;
	struct usb_device *usbdev = interface_to_usbdev(intf);

	tty_dev = usb_get_intfdata(intf);

	mux_dev = tty_dev->priv_dev;

	release_usb(mux_dev);
	unregister_lte_tty_device(tty_dev);

	kfree(mux_dev);
	kfree(tty_dev);

	usb_put_dev(usbdev);
}

static int gdm_mux_suspend(struct usb_interface *intf, pm_message_t pm_msg)
{
	struct tty_dev *tty_dev;
	struct mux_dev *mux_dev;
	struct rx_cxt *rx;
	struct mux_rx *r, *r_next;
	unsigned long flags;

	tty_dev = usb_get_intfdata(intf);
	mux_dev = tty_dev->priv_dev;
	rx = &mux_dev->rx;

	if (mux_dev->usb_state != PM_NORMAL) {
		dev_err(intf->usb_dev, "usb suspend - invalid state\n");
		return -1;
	}

	mux_dev->usb_state = PM_SUSPEND;


	spin_lock_irqsave(&rx->submit_list_lock, flags);
	list_for_each_entry_safe(r, r_next, &rx->rx_submit_list,
				 rx_submit_list) {
		spin_unlock_irqrestore(&rx->submit_list_lock, flags);
		usb_kill_urb(r->urb);
		spin_lock_irqsave(&rx->submit_list_lock, flags);
	}
	spin_unlock_irqrestore(&rx->submit_list_lock, flags);

	return 0;
}

static int gdm_mux_resume(struct usb_interface *intf)
{
	struct tty_dev *tty_dev;
	struct mux_dev *mux_dev;
	u8 i;

	tty_dev = usb_get_intfdata(intf);
	mux_dev = tty_dev->priv_dev;

	if (mux_dev->usb_state != PM_SUSPEND) {
		dev_err(intf->usb_dev, "usb resume - invalid state\n");
		return -1;
	}

	mux_dev->usb_state = PM_NORMAL;

	for (i = 0; i < MAX_ISSUE_NUM; i++)
		gdm_mux_recv(mux_dev, mux_dev->rx_cb);

	return 0;
}

static struct usb_driver gdm_mux_driver = {
	.name = "gdm_mux",
	.probe = gdm_mux_probe,
	.disconnect = gdm_mux_disconnect,
	.id_table = id_table,
	.supports_autosuspend = 1,
	.suspend = gdm_mux_suspend,
	.resume = gdm_mux_resume,
	.reset_resume = gdm_mux_resume,
};

static int __init gdm_usb_mux_init(void)
{

	mux_rx_wq = create_workqueue("mux_rx_wq");
	if (mux_rx_wq == NULL) {
		pr_err("work queue create fail\n");
		return -1;
	}

	register_lte_tty_driver();

	return usb_register(&gdm_mux_driver);
}

static void __exit gdm_usb_mux_exit(void)
{
	unregister_lte_tty_driver();

	if (mux_rx_wq) {
		flush_workqueue(mux_rx_wq);
		destroy_workqueue(mux_rx_wq);
	}

	usb_deregister(&gdm_mux_driver);
}

module_init(gdm_usb_mux_init);
module_exit(gdm_usb_mux_exit);

MODULE_DESCRIPTION("GCT LTE TTY Device Driver");
MODULE_LICENSE("GPL");
