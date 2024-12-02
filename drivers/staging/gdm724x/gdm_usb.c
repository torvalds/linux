// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/usb/cdc.h>
#include <linux/wait.h>
#include <linux/if_ether.h>
#include <linux/pm_runtime.h>

#include "gdm_usb.h"
#include "gdm_lte.h"
#include "hci.h"
#include "hci_packet.h"
#include "gdm_endian.h"

#define USB_DEVICE_CDC_DATA(vid, pid) \
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE | \
		USB_DEVICE_ID_MATCH_INT_CLASS | \
		USB_DEVICE_ID_MATCH_INT_SUBCLASS,\
	.idVendor = vid,\
	.idProduct = pid,\
	.bInterfaceClass = USB_CLASS_COMM,\
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ETHERNET

#define USB_DEVICE_MASS_DATA(vid, pid) \
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE | \
		USB_DEVICE_ID_MATCH_INT_INFO,\
	.idVendor = vid,\
	.idProduct = pid,\
	.bInterfaceSubClass = USB_SC_SCSI, \
	.bInterfaceClass = USB_CLASS_MASS_STORAGE,\
	.bInterfaceProtocol = USB_PR_BULK

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE_CDC_DATA(VID_GCT, PID_GDM7240) }, /* GCT GDM7240 */
	{ USB_DEVICE_CDC_DATA(VID_GCT, PID_GDM7243) }, /* GCT GDM7243 */
	{ }
};

MODULE_DEVICE_TABLE(usb, id_table);

static void do_tx(struct work_struct *work);
static void do_rx(struct work_struct *work);

static int gdm_usb_recv(void *priv_dev,
			int (*cb)(void *cb_data,
				  void *data, int len, int context),
			void *cb_data,
			int context);

static int request_mac_address(struct lte_udev *udev)
{
	struct hci_packet *hci;
	struct usb_device *usbdev = udev->usbdev;
	int actual;
	int ret = -1;

	hci = kmalloc(struct_size(hci, data, 1), GFP_KERNEL);
	if (!hci)
		return -ENOMEM;

	hci->cmd_evt = gdm_cpu_to_dev16(udev->gdm_ed, LTE_GET_INFORMATION);
	hci->len = gdm_cpu_to_dev16(udev->gdm_ed, 1);
	hci->data[0] = MAC_ADDRESS;

	ret = usb_bulk_msg(usbdev, usb_sndbulkpipe(usbdev, 2), hci, 5,
			   &actual, 1000);

	udev->request_mac_addr = 1;
	kfree(hci);

	return ret;
}

static struct usb_tx *alloc_tx_struct(int len)
{
	struct usb_tx *t = NULL;
	int ret = 0;

	t = kzalloc(sizeof(*t), GFP_ATOMIC);
	if (!t) {
		ret = -ENOMEM;
		goto out;
	}

	t->urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!(len % 512))
		len++;

	t->buf = kmalloc(len, GFP_ATOMIC);
	if (!t->urb || !t->buf) {
		ret = -ENOMEM;
		goto out;
	}

out:
	if (ret < 0) {
		if (t) {
			usb_free_urb(t->urb);
			kfree(t->buf);
			kfree(t);
		}
		return NULL;
	}

	return t;
}

static struct usb_tx_sdu *alloc_tx_sdu_struct(void)
{
	struct usb_tx_sdu *t_sdu;

	t_sdu = kzalloc(sizeof(*t_sdu), GFP_KERNEL);
	if (!t_sdu)
		return NULL;

	t_sdu->buf = kmalloc(SDU_BUF_SIZE, GFP_KERNEL);
	if (!t_sdu->buf) {
		kfree(t_sdu);
		return NULL;
	}

	return t_sdu;
}

static void free_tx_struct(struct usb_tx *t)
{
	if (t) {
		usb_free_urb(t->urb);
		kfree(t->buf);
		kfree(t);
	}
}

static void free_tx_sdu_struct(struct usb_tx_sdu *t_sdu)
{
	if (t_sdu) {
		kfree(t_sdu->buf);
		kfree(t_sdu);
	}
}

static struct usb_tx_sdu *get_tx_sdu_struct(struct tx_cxt *tx, int *no_spc)
{
	struct usb_tx_sdu *t_sdu;

	if (list_empty(&tx->free_list))
		return NULL;

	t_sdu = list_entry(tx->free_list.next, struct usb_tx_sdu, list);
	list_del(&t_sdu->list);

	tx->avail_count--;

	*no_spc = list_empty(&tx->free_list) ? 1 : 0;

	return t_sdu;
}

static void put_tx_struct(struct tx_cxt *tx, struct usb_tx_sdu *t_sdu)
{
	list_add_tail(&t_sdu->list, &tx->free_list);
	tx->avail_count++;
}

static struct usb_rx *alloc_rx_struct(void)
{
	struct usb_rx *r = NULL;
	int ret = 0;

	r = kmalloc(sizeof(*r), GFP_KERNEL);
	if (!r) {
		ret = -ENOMEM;
		goto out;
	}

	r->urb = usb_alloc_urb(0, GFP_KERNEL);
	r->buf = kmalloc(RX_BUF_SIZE, GFP_KERNEL);
	if (!r->urb || !r->buf) {
		ret = -ENOMEM;
		goto out;
	}
out:

	if (ret < 0) {
		if (r) {
			usb_free_urb(r->urb);
			kfree(r->buf);
			kfree(r);
		}
		return NULL;
	}

	return r;
}

static void free_rx_struct(struct usb_rx *r)
{
	if (r) {
		usb_free_urb(r->urb);
		kfree(r->buf);
		kfree(r);
	}
}

static struct usb_rx *get_rx_struct(struct rx_cxt *rx, int *no_spc)
{
	struct usb_rx *r;
	unsigned long flags;

	spin_lock_irqsave(&rx->rx_lock, flags);

	if (list_empty(&rx->free_list)) {
		spin_unlock_irqrestore(&rx->rx_lock, flags);
		return NULL;
	}

	r = list_entry(rx->free_list.next, struct usb_rx, free_list);
	list_del(&r->free_list);

	rx->avail_count--;

	*no_spc = list_empty(&rx->free_list) ? 1 : 0;

	spin_unlock_irqrestore(&rx->rx_lock, flags);

	return r;
}

static void put_rx_struct(struct rx_cxt *rx, struct usb_rx *r)
{
	unsigned long flags;

	spin_lock_irqsave(&rx->rx_lock, flags);

	list_add_tail(&r->free_list, &rx->free_list);
	rx->avail_count++;

	spin_unlock_irqrestore(&rx->rx_lock, flags);
}

static void release_usb(struct lte_udev *udev)
{
	struct rx_cxt	*rx = &udev->rx;
	struct tx_cxt	*tx = &udev->tx;
	struct usb_tx	*t, *t_next;
	struct usb_rx	*r, *r_next;
	struct usb_tx_sdu	*t_sdu, *t_sdu_next;
	unsigned long flags;

	spin_lock_irqsave(&tx->lock, flags);
	list_for_each_entry_safe(t_sdu, t_sdu_next, &tx->sdu_list, list) {
		list_del(&t_sdu->list);
		free_tx_sdu_struct(t_sdu);
	}

	list_for_each_entry_safe(t, t_next, &tx->hci_list, list) {
		list_del(&t->list);
		free_tx_struct(t);
	}

	list_for_each_entry_safe(t_sdu, t_sdu_next, &tx->free_list, list) {
		list_del(&t_sdu->list);
		free_tx_sdu_struct(t_sdu);
	}
	spin_unlock_irqrestore(&tx->lock, flags);

	spin_lock_irqsave(&rx->submit_lock, flags);
	list_for_each_entry_safe(r, r_next, &rx->rx_submit_list,
				 rx_submit_list) {
		spin_unlock_irqrestore(&rx->submit_lock, flags);
		usb_kill_urb(r->urb);
		spin_lock_irqsave(&rx->submit_lock, flags);
	}
	spin_unlock_irqrestore(&rx->submit_lock, flags);

	spin_lock_irqsave(&rx->rx_lock, flags);
	list_for_each_entry_safe(r, r_next, &rx->free_list, free_list) {
		list_del(&r->free_list);
		free_rx_struct(r);
	}
	spin_unlock_irqrestore(&rx->rx_lock, flags);

	spin_lock_irqsave(&rx->to_host_lock, flags);
	list_for_each_entry_safe(r, r_next, &rx->to_host_list, to_host_list) {
		if (r->index == (void *)udev) {
			list_del(&r->to_host_list);
			free_rx_struct(r);
		}
	}
	spin_unlock_irqrestore(&rx->to_host_lock, flags);
}

static int init_usb(struct lte_udev *udev)
{
	int ret = 0;
	int i;
	struct tx_cxt *tx = &udev->tx;
	struct rx_cxt *rx = &udev->rx;
	struct usb_tx_sdu *t_sdu = NULL;
	struct usb_rx *r = NULL;

	udev->send_complete = 1;
	udev->tx_stop = 0;
	udev->request_mac_addr = 0;
	udev->usb_state = PM_NORMAL;

	INIT_LIST_HEAD(&tx->sdu_list);
	INIT_LIST_HEAD(&tx->hci_list);
	INIT_LIST_HEAD(&tx->free_list);
	INIT_LIST_HEAD(&rx->rx_submit_list);
	INIT_LIST_HEAD(&rx->free_list);
	INIT_LIST_HEAD(&rx->to_host_list);
	spin_lock_init(&tx->lock);
	spin_lock_init(&rx->rx_lock);
	spin_lock_init(&rx->submit_lock);
	spin_lock_init(&rx->to_host_lock);

	tx->avail_count = 0;
	rx->avail_count = 0;

	udev->rx_cb = NULL;

	for (i = 0; i < MAX_NUM_SDU_BUF; i++) {
		t_sdu = alloc_tx_sdu_struct();
		if (!t_sdu) {
			ret = -ENOMEM;
			goto fail;
		}

		list_add(&t_sdu->list, &tx->free_list);
		tx->avail_count++;
	}

	for (i = 0; i < MAX_RX_SUBMIT_COUNT * 2; i++) {
		r = alloc_rx_struct();
		if (!r) {
			ret = -ENOMEM;
			goto fail;
		}

		list_add(&r->free_list, &rx->free_list);
		rx->avail_count++;
	}
	INIT_DELAYED_WORK(&udev->work_tx, do_tx);
	INIT_DELAYED_WORK(&udev->work_rx, do_rx);
	return 0;
fail:
	release_usb(udev);
	return ret;
}

static int set_mac_address(u8 *data, void *arg)
{
	struct phy_dev *phy_dev = arg;
	struct lte_udev *udev = phy_dev->priv_dev;
	struct tlv *tlv = (struct tlv *)data;
	u8 mac_address[ETH_ALEN] = {0, };

	if (tlv->type == MAC_ADDRESS && udev->request_mac_addr) {
		memcpy(mac_address, tlv->data, tlv->len);

		if (register_lte_device(phy_dev,
					&udev->intf->dev, mac_address) < 0)
			pr_err("register lte device failed\n");

		udev->request_mac_addr = 0;

		return 1;
	}

	return 0;
}

static void do_rx(struct work_struct *work)
{
	struct lte_udev *udev =
		container_of(work, struct lte_udev, work_rx.work);
	struct rx_cxt *rx = &udev->rx;
	struct usb_rx *r;
	struct hci_packet *hci;
	struct phy_dev *phy_dev;
	u16 cmd_evt;
	int ret;
	unsigned long flags;

	while (1) {
		spin_lock_irqsave(&rx->to_host_lock, flags);
		if (list_empty(&rx->to_host_list)) {
			spin_unlock_irqrestore(&rx->to_host_lock, flags);
			break;
		}
		r = list_entry(rx->to_host_list.next,
			       struct usb_rx, to_host_list);
		list_del(&r->to_host_list);
		spin_unlock_irqrestore(&rx->to_host_lock, flags);

		phy_dev = r->cb_data;
		udev = phy_dev->priv_dev;
		hci = (struct hci_packet *)r->buf;
		cmd_evt = gdm_dev16_to_cpu(udev->gdm_ed, hci->cmd_evt);

		switch (cmd_evt) {
		case LTE_GET_INFORMATION_RESULT:
			if (set_mac_address(hci->data, r->cb_data) == 0) {
				r->callback(r->cb_data,
					    r->buf,
					    r->urb->actual_length,
					    KERNEL_THREAD);
			}
			break;

		default:
			if (r->callback) {
				ret = r->callback(r->cb_data,
						  r->buf,
						  r->urb->actual_length,
						  KERNEL_THREAD);

				if (ret == -EAGAIN)
					pr_err("failed to send received data\n");
			}
			break;
		}

		put_rx_struct(rx, r);

		gdm_usb_recv(udev,
			     r->callback,
			     r->cb_data,
			     USB_COMPLETE);
	}
}

static void remove_rx_submit_list(struct usb_rx *r, struct rx_cxt *rx)
{
	unsigned long flags;
	struct usb_rx	*r_remove, *r_remove_next;

	spin_lock_irqsave(&rx->submit_lock, flags);
	list_for_each_entry_safe(r_remove, r_remove_next,
				 &rx->rx_submit_list, rx_submit_list) {
		if (r == r_remove) {
			list_del(&r->rx_submit_list);
			break;
		}
	}
	spin_unlock_irqrestore(&rx->submit_lock, flags);
}

static void gdm_usb_rcv_complete(struct urb *urb)
{
	struct usb_rx *r = urb->context;
	struct rx_cxt *rx = r->rx;
	unsigned long flags;
	struct lte_udev *udev = container_of(r->rx, struct lte_udev, rx);
	struct usb_device *usbdev = udev->usbdev;

	remove_rx_submit_list(r, rx);

	if (!urb->status && r->callback) {
		spin_lock_irqsave(&rx->to_host_lock, flags);
		list_add_tail(&r->to_host_list, &rx->to_host_list);
		schedule_work(&udev->work_rx.work);
		spin_unlock_irqrestore(&rx->to_host_lock, flags);
	} else {
		if (urb->status && udev->usb_state == PM_NORMAL)
			dev_err(&urb->dev->dev, "%s: urb status error %d\n",
				__func__, urb->status);

		put_rx_struct(rx, r);
	}

	usb_mark_last_busy(usbdev);
}

static int gdm_usb_recv(void *priv_dev,
			int (*cb)(void *cb_data,
				  void *data, int len, int context),
			void *cb_data,
			int context)
{
	struct lte_udev *udev = priv_dev;
	struct usb_device *usbdev = udev->usbdev;
	struct rx_cxt *rx = &udev->rx;
	struct usb_rx *r;
	int no_spc;
	int ret;
	unsigned long flags;

	if (!udev->usbdev) {
		pr_err("invalid device\n");
		return -ENODEV;
	}

	r = get_rx_struct(rx, &no_spc);
	if (!r) {
		pr_err("Out of Memory\n");
		return -ENOMEM;
	}

	udev->rx_cb = cb;
	r->callback = cb;
	r->cb_data = cb_data;
	r->index = (void *)udev;
	r->rx = rx;

	usb_fill_bulk_urb(r->urb,
			  usbdev,
			  usb_rcvbulkpipe(usbdev, 0x83),
			  r->buf,
			  RX_BUF_SIZE,
			  gdm_usb_rcv_complete,
			  r);

	spin_lock_irqsave(&rx->submit_lock, flags);
	list_add_tail(&r->rx_submit_list, &rx->rx_submit_list);
	spin_unlock_irqrestore(&rx->submit_lock, flags);

	if (context == KERNEL_THREAD)
		ret = usb_submit_urb(r->urb, GFP_KERNEL);
	else
		ret = usb_submit_urb(r->urb, GFP_ATOMIC);

	if (ret) {
		spin_lock_irqsave(&rx->submit_lock, flags);
		list_del(&r->rx_submit_list);
		spin_unlock_irqrestore(&rx->submit_lock, flags);

		pr_err("usb_submit_urb failed (%p)\n", r);
		put_rx_struct(rx, r);
	}

	return ret;
}

static void gdm_usb_send_complete(struct urb *urb)
{
	struct usb_tx *t = urb->context;
	struct tx_cxt *tx = t->tx;
	struct lte_udev *udev = container_of(tx, struct lte_udev, tx);
	unsigned long flags;

	if (urb->status == -ECONNRESET) {
		dev_info(&urb->dev->dev, "CONNRESET\n");
		return;
	}

	if (t->callback)
		t->callback(t->cb_data);

	free_tx_struct(t);

	spin_lock_irqsave(&tx->lock, flags);
	udev->send_complete = 1;
	schedule_work(&udev->work_tx.work);
	spin_unlock_irqrestore(&tx->lock, flags);
}

static int send_tx_packet(struct usb_device *usbdev, struct usb_tx *t, u32 len)
{
	int ret = 0;

	if (!(len % 512))
		len++;

	usb_fill_bulk_urb(t->urb,
			  usbdev,
			  usb_sndbulkpipe(usbdev, 2),
			  t->buf,
			  len,
			  gdm_usb_send_complete,
			  t);

	ret = usb_submit_urb(t->urb, GFP_ATOMIC);

	if (ret)
		dev_err(&usbdev->dev, "usb_submit_urb failed: %d\n",
			ret);

	usb_mark_last_busy(usbdev);

	return ret;
}

static u32 packet_aggregation(struct lte_udev *udev, u8 *send_buf)
{
	struct tx_cxt *tx = &udev->tx;
	struct usb_tx_sdu *t_sdu = NULL;
	struct multi_sdu *multi_sdu = (struct multi_sdu *)send_buf;
	u16 send_len = 0;
	u16 num_packet = 0;
	unsigned long flags;

	multi_sdu->cmd_evt = gdm_cpu_to_dev16(udev->gdm_ed, LTE_TX_MULTI_SDU);

	while (num_packet < MAX_PACKET_IN_MULTI_SDU) {
		spin_lock_irqsave(&tx->lock, flags);
		if (list_empty(&tx->sdu_list)) {
			spin_unlock_irqrestore(&tx->lock, flags);
			break;
		}

		t_sdu = list_entry(tx->sdu_list.next, struct usb_tx_sdu, list);
		if (send_len + t_sdu->len > MAX_SDU_SIZE) {
			spin_unlock_irqrestore(&tx->lock, flags);
			break;
		}

		list_del(&t_sdu->list);
		spin_unlock_irqrestore(&tx->lock, flags);

		memcpy(multi_sdu->data + send_len, t_sdu->buf, t_sdu->len);

		send_len += (t_sdu->len + 3) & 0xfffc;
		num_packet++;

		if (tx->avail_count > 10)
			t_sdu->callback(t_sdu->cb_data);

		spin_lock_irqsave(&tx->lock, flags);
		put_tx_struct(tx, t_sdu);
		spin_unlock_irqrestore(&tx->lock, flags);
	}

	multi_sdu->len = gdm_cpu_to_dev16(udev->gdm_ed, send_len);
	multi_sdu->num_packet = gdm_cpu_to_dev16(udev->gdm_ed, num_packet);

	return send_len + offsetof(struct multi_sdu, data);
}

static void do_tx(struct work_struct *work)
{
	struct lte_udev *udev =
		container_of(work, struct lte_udev, work_tx.work);
	struct usb_device *usbdev = udev->usbdev;
	struct tx_cxt *tx = &udev->tx;
	struct usb_tx *t = NULL;
	int is_send = 0;
	u32 len = 0;
	unsigned long flags;

	if (!usb_autopm_get_interface(udev->intf))
		usb_autopm_put_interface(udev->intf);

	if (udev->usb_state == PM_SUSPEND)
		return;

	spin_lock_irqsave(&tx->lock, flags);
	if (!udev->send_complete) {
		spin_unlock_irqrestore(&tx->lock, flags);
		return;
	}
	udev->send_complete = 0;

	if (!list_empty(&tx->hci_list)) {
		t = list_entry(tx->hci_list.next, struct usb_tx, list);
		list_del(&t->list);
		len = t->len;
		t->is_sdu = 0;
		is_send = 1;
	} else if (!list_empty(&tx->sdu_list)) {
		if (udev->tx_stop) {
			udev->send_complete = 1;
			spin_unlock_irqrestore(&tx->lock, flags);
			return;
		}

		t = alloc_tx_struct(TX_BUF_SIZE);
		if (!t) {
			spin_unlock_irqrestore(&tx->lock, flags);
			return;
		}
		t->callback = NULL;
		t->tx = tx;
		t->is_sdu = 1;
		is_send = 1;
	}

	if (!is_send) {
		udev->send_complete = 1;
		spin_unlock_irqrestore(&tx->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&tx->lock, flags);

	if (t->is_sdu)
		len = packet_aggregation(udev, t->buf);

	if (send_tx_packet(usbdev, t, len)) {
		pr_err("send_tx_packet failed\n");
		t->callback = NULL;
		gdm_usb_send_complete(t->urb);
	}
}

#define SDU_PARAM_LEN 12
static int gdm_usb_sdu_send(void *priv_dev, void *data, int len,
			    unsigned int dft_eps_ID, unsigned int eps_ID,
			    void (*cb)(void *data), void *cb_data,
			    int dev_idx, int nic_type)
{
	struct lte_udev *udev = priv_dev;
	struct tx_cxt *tx = &udev->tx;
	struct usb_tx_sdu *t_sdu;
	struct sdu *sdu = NULL;
	unsigned long flags;
	int no_spc = 0;
	u16 send_len;

	if (!udev->usbdev) {
		pr_err("sdu send - invalid device\n");
		return TX_NO_DEV;
	}

	spin_lock_irqsave(&tx->lock, flags);
	t_sdu = get_tx_sdu_struct(tx, &no_spc);
	spin_unlock_irqrestore(&tx->lock, flags);

	if (!t_sdu) {
		pr_err("sdu send - free list empty\n");
		return TX_NO_SPC;
	}

	sdu = (struct sdu *)t_sdu->buf;
	sdu->cmd_evt = gdm_cpu_to_dev16(udev->gdm_ed, LTE_TX_SDU);
	if (nic_type == NIC_TYPE_ARP) {
		send_len = len + SDU_PARAM_LEN;
		memcpy(sdu->data, data, len);
	} else {
		send_len = len - ETH_HLEN;
		send_len += SDU_PARAM_LEN;
		memcpy(sdu->data, data + ETH_HLEN, len - ETH_HLEN);
	}

	sdu->len = gdm_cpu_to_dev16(udev->gdm_ed, send_len);
	sdu->dft_eps_ID = gdm_cpu_to_dev32(udev->gdm_ed, dft_eps_ID);
	sdu->bearer_ID = gdm_cpu_to_dev32(udev->gdm_ed, eps_ID);
	sdu->nic_type = gdm_cpu_to_dev32(udev->gdm_ed, nic_type);

	t_sdu->len = send_len + HCI_HEADER_SIZE;
	t_sdu->callback = cb;
	t_sdu->cb_data = cb_data;

	spin_lock_irqsave(&tx->lock, flags);
	list_add_tail(&t_sdu->list, &tx->sdu_list);
	schedule_work(&udev->work_tx.work);
	spin_unlock_irqrestore(&tx->lock, flags);

	if (no_spc)
		return TX_NO_BUFFER;

	return 0;
}

static int gdm_usb_hci_send(void *priv_dev, void *data, int len,
			    void (*cb)(void *data), void *cb_data)
{
	struct lte_udev *udev = priv_dev;
	struct tx_cxt *tx = &udev->tx;
	struct usb_tx *t;
	unsigned long flags;

	if (!udev->usbdev) {
		pr_err("hci send - invalid device\n");
		return -ENODEV;
	}

	t = alloc_tx_struct(len);
	if (!t) {
		pr_err("hci_send - out of memory\n");
		return -ENOMEM;
	}

	memcpy(t->buf, data, len);
	t->callback = cb;
	t->cb_data = cb_data;
	t->len = len;
	t->tx = tx;
	t->is_sdu = 0;

	spin_lock_irqsave(&tx->lock, flags);
	list_add_tail(&t->list, &tx->hci_list);
	schedule_work(&udev->work_tx.work);
	spin_unlock_irqrestore(&tx->lock, flags);

	return 0;
}

static u8 gdm_usb_get_endian(void *priv_dev)
{
	struct lte_udev *udev = priv_dev;

	return udev->gdm_ed;
}

static int gdm_usb_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	int ret = 0;
	struct phy_dev *phy_dev = NULL;
	struct lte_udev *udev = NULL;
	u16 idVendor, idProduct;
	int bInterfaceNumber;
	struct usb_device *usbdev = interface_to_usbdev(intf);

	bInterfaceNumber = intf->cur_altsetting->desc.bInterfaceNumber;
	idVendor = __le16_to_cpu(usbdev->descriptor.idVendor);
	idProduct = __le16_to_cpu(usbdev->descriptor.idProduct);

	pr_info("net vid = 0x%04x pid = 0x%04x\n", idVendor, idProduct);

	if (bInterfaceNumber > NETWORK_INTERFACE) {
		pr_info("not a network device\n");
		return -ENODEV;
	}

	phy_dev = kzalloc(sizeof(*phy_dev), GFP_KERNEL);
	if (!phy_dev)
		return -ENOMEM;

	udev = kzalloc(sizeof(*udev), GFP_KERNEL);
	if (!udev) {
		ret = -ENOMEM;
		goto err_udev;
	}

	phy_dev->priv_dev = (void *)udev;
	phy_dev->send_hci_func = gdm_usb_hci_send;
	phy_dev->send_sdu_func = gdm_usb_sdu_send;
	phy_dev->rcv_func = gdm_usb_recv;
	phy_dev->get_endian = gdm_usb_get_endian;

	udev->usbdev = usbdev;
	ret = init_usb(udev);
	if (ret < 0) {
		dev_err(intf->usb_dev, "init_usb func failed\n");
		goto err_init_usb;
	}
	udev->intf = intf;

	intf->needs_remote_wakeup = 1;
	usb_enable_autosuspend(usbdev);
	pm_runtime_set_autosuspend_delay(&usbdev->dev, AUTO_SUSPEND_TIMER);

	/* List up hosts with big endians, otherwise,
	 * defaults to little endian
	 */
	if (idProduct == PID_GDM7243)
		udev->gdm_ed = ENDIANNESS_BIG;
	else
		udev->gdm_ed = ENDIANNESS_LITTLE;

	ret = request_mac_address(udev);
	if (ret < 0) {
		dev_err(intf->usb_dev, "request Mac address failed\n");
		goto err_mac_address;
	}

	start_rx_proc(phy_dev);
	usb_get_dev(usbdev);
	usb_set_intfdata(intf, phy_dev);

	return 0;

err_mac_address:
	release_usb(udev);
err_init_usb:
	kfree(udev);
err_udev:
	kfree(phy_dev);

	return ret;
}

static void gdm_usb_disconnect(struct usb_interface *intf)
{
	struct phy_dev *phy_dev;
	struct lte_udev *udev;
	struct usb_device *usbdev;

	usbdev = interface_to_usbdev(intf);
	phy_dev = usb_get_intfdata(intf);

	udev = phy_dev->priv_dev;
	unregister_lte_device(phy_dev);

	release_usb(udev);

	kfree(udev);
	udev = NULL;

	kfree(phy_dev);
	phy_dev = NULL;

	usb_put_dev(usbdev);
}

static int gdm_usb_suspend(struct usb_interface *intf, pm_message_t pm_msg)
{
	struct phy_dev *phy_dev;
	struct lte_udev *udev;
	struct rx_cxt *rx;
	struct usb_rx *r;
	struct usb_rx *r_next;
	unsigned long flags;

	phy_dev = usb_get_intfdata(intf);
	udev = phy_dev->priv_dev;
	rx = &udev->rx;
	if (udev->usb_state != PM_NORMAL) {
		dev_err(intf->usb_dev, "usb suspend - invalid state\n");
		return -1;
	}

	udev->usb_state = PM_SUSPEND;

	spin_lock_irqsave(&rx->submit_lock, flags);
	list_for_each_entry_safe(r, r_next, &rx->rx_submit_list,
				 rx_submit_list) {
		spin_unlock_irqrestore(&rx->submit_lock, flags);
		usb_kill_urb(r->urb);
		spin_lock_irqsave(&rx->submit_lock, flags);
	}
	spin_unlock_irqrestore(&rx->submit_lock, flags);

	cancel_work_sync(&udev->work_tx.work);
	cancel_work_sync(&udev->work_rx.work);

	return 0;
}

static int gdm_usb_resume(struct usb_interface *intf)
{
	struct phy_dev *phy_dev;
	struct lte_udev *udev;
	struct tx_cxt *tx;
	struct rx_cxt *rx;
	unsigned long flags;
	int issue_count;
	int i;

	phy_dev = usb_get_intfdata(intf);
	udev = phy_dev->priv_dev;
	rx = &udev->rx;

	if (udev->usb_state != PM_SUSPEND) {
		dev_err(intf->usb_dev, "usb resume - invalid state\n");
		return -1;
	}
	udev->usb_state = PM_NORMAL;

	spin_lock_irqsave(&rx->rx_lock, flags);
	issue_count = rx->avail_count - MAX_RX_SUBMIT_COUNT;
	spin_unlock_irqrestore(&rx->rx_lock, flags);

	if (issue_count >= 0) {
		for (i = 0; i < issue_count; i++)
			gdm_usb_recv(phy_dev->priv_dev,
				     udev->rx_cb,
				     phy_dev,
				     USB_COMPLETE);
	}

	tx = &udev->tx;
	spin_lock_irqsave(&tx->lock, flags);
	schedule_work(&udev->work_tx.work);
	spin_unlock_irqrestore(&tx->lock, flags);

	return 0;
}

static struct usb_driver gdm_usb_lte_driver = {
	.name = "gdm_lte",
	.probe = gdm_usb_probe,
	.disconnect = gdm_usb_disconnect,
	.id_table = id_table,
	.supports_autosuspend = 1,
	.suspend = gdm_usb_suspend,
	.resume = gdm_usb_resume,
	.reset_resume = gdm_usb_resume,
};

static int __init gdm_usb_lte_init(void)
{
	if (gdm_lte_event_init() < 0) {
		pr_err("error creating event\n");
		return -1;
	}

	return usb_register(&gdm_usb_lte_driver);
}

static void __exit gdm_usb_lte_exit(void)
{
	gdm_lte_event_exit();

	usb_deregister(&gdm_usb_lte_driver);
}

module_init(gdm_usb_lte_init);
module_exit(gdm_usb_lte_exit);

MODULE_VERSION(DRIVER_VERSION);
MODULE_DESCRIPTION("GCT LTE USB Device Driver");
MODULE_LICENSE("GPL");
