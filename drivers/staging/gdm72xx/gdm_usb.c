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

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <asm/byteorder.h>

#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
#ifndef CONFIG_USB_SUSPEND
#error "USB host doesn't support USB Selective Suspend."
#endif
#endif

#include "gdm_usb.h"
#include "gdm_wimax.h"
#include "usb_boot.h"
#include "hci.h"

#include "usb_ids.h"

MODULE_DEVICE_TABLE(usb, id_table);

#define TX_BUF_SIZE	2048
#if defined(CONFIG_WIMAX_GDM72XX_WIMAX2)
#define RX_BUF_SIZE	(128*1024)	/* For packet aggregation */
#else
#define RX_BUF_SIZE	2048
#endif

#define GDM7205_PADDING		256

#define H2B(x)		__cpu_to_be16(x)
#define B2H(x)		__be16_to_cpu(x)
#define DB2H(x)		__be32_to_cpu(x)

#define DOWNLOAD_CONF_VALUE		0x21

#ifdef CONFIG_WIMAX_GDM72XX_K_MODE

static DECLARE_WAIT_QUEUE_HEAD(k_wait);
static LIST_HEAD(k_list);
static DEFINE_SPINLOCK(k_lock);
static int k_mode_stop;

#define K_WAIT_TIME	(2 * HZ / 100)

#endif /* CONFIG_WIMAX_GDM72XX_K_MODE */

static int init_usb(struct usbwm_dev *udev);
static void release_usb(struct usbwm_dev *udev);

/*#define DEBUG */
#ifdef DEBUG
static void hexdump(char *title, u8 *data, int len)
{
	int i;

	printk(KERN_DEBUG "%s: length = %d\n", title, len);
	for (i = 0; i < len; i++) {
		printk(KERN_DEBUG "%02x ", data[i]);
		if ((i & 0xf) == 0xf)
			printk(KERN_DEBUG "\n");
	}
	printk(KERN_DEBUG "\n");
}
#endif

static struct usb_tx *alloc_tx_struct(struct tx_cxt *tx)
{
	struct usb_tx *t = NULL;

	t = kmalloc(sizeof(*t), GFP_ATOMIC);
	if (t == NULL)
		goto out;

	memset(t, 0, sizeof(*t));

	t->urb = usb_alloc_urb(0, GFP_ATOMIC);
	t->buf = kmalloc(TX_BUF_SIZE, GFP_ATOMIC);
	if (t->urb == NULL || t->buf == NULL)
		goto out;

	t->tx_cxt = tx;

	return t;
out:
	if (t) {
		usb_free_urb(t->urb);
		kfree(t->buf);
		kfree(t);
	}
	return NULL;
}

static void free_tx_struct(struct usb_tx *t)
{
	if (t) {
		usb_free_urb(t->urb);
		kfree(t->buf);
		kfree(t);
	}
}

static struct usb_rx *alloc_rx_struct(struct rx_cxt *rx)
{
	struct usb_rx *r = NULL;

	r = kmalloc(sizeof(*r), GFP_ATOMIC);
	if (r == NULL)
		goto out;

	memset(r, 0, sizeof(*r));

	r->urb = usb_alloc_urb(0, GFP_ATOMIC);
	r->buf = kmalloc(RX_BUF_SIZE, GFP_ATOMIC);
	if (r->urb == NULL || r->buf == NULL)
		goto out;

	r->rx_cxt = rx;
	return r;
out:
	if (r) {
		usb_free_urb(r->urb);
		kfree(r->buf);
		kfree(r);
	}
	return NULL;
}

static void free_rx_struct(struct usb_rx *r)
{
	if (r) {
		usb_free_urb(r->urb);
		kfree(r->buf);
		kfree(r);
	}
}

/* Before this function is called, spin lock should be locked. */
static struct usb_tx *get_tx_struct(struct tx_cxt *tx, int *no_spc)
{
	struct usb_tx *t;

	if (list_empty(&tx->free_list)) {
		*no_spc = 1;
		return NULL;
	}

	t = list_entry(tx->free_list.next, struct usb_tx, list);
	list_del(&t->list);

	*no_spc = list_empty(&tx->free_list) ? 1 : 0;

	return t;
}

/* Before this function is called, spin lock should be locked. */
static void put_tx_struct(struct tx_cxt *tx, struct usb_tx *t)
{
	list_add_tail(&t->list, &tx->free_list);
}

/* Before this function is called, spin lock should be locked. */
static struct usb_rx *get_rx_struct(struct rx_cxt *rx)
{
	struct usb_rx *r;

	if (list_empty(&rx->free_list)) {
		r = alloc_rx_struct(rx);
		if (r == NULL)
			return NULL;

		list_add(&r->list, &rx->free_list);
	}

	r = list_entry(rx->free_list.next, struct usb_rx, list);
	list_del(&r->list);
	list_add_tail(&r->list, &rx->used_list);

	return r;
}

/* Before this function is called, spin lock should be locked. */
static void put_rx_struct(struct rx_cxt *rx, struct usb_rx *r)
{
	list_del(&r->list);
	list_add(&r->list, &rx->free_list);
}

static int init_usb(struct usbwm_dev *udev)
{
	int ret = 0, i;
	struct tx_cxt	*tx = &udev->tx;
	struct rx_cxt	*rx = &udev->rx;
	struct usb_tx	*t;
	struct usb_rx	*r;

	INIT_LIST_HEAD(&tx->free_list);
	INIT_LIST_HEAD(&tx->sdu_list);
	INIT_LIST_HEAD(&tx->hci_list);
#if defined(CONFIG_WIMAX_GDM72XX_USB_PM) || defined(CONFIG_WIMAX_GDM72XX_K_MODE)
	INIT_LIST_HEAD(&tx->pending_list);
#endif

	INIT_LIST_HEAD(&rx->free_list);
	INIT_LIST_HEAD(&rx->used_list);

	spin_lock_init(&tx->lock);
	spin_lock_init(&rx->lock);

	for (i = 0; i < MAX_NR_SDU_BUF; i++) {
		t = alloc_tx_struct(tx);
		if (t == NULL) {
			ret = -ENOMEM;
			goto fail;
		}
		list_add(&t->list, &tx->free_list);
	}

	r = alloc_rx_struct(rx);
	if (r == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	list_add(&r->list, &rx->free_list);
	return ret;

fail:
	release_usb(udev);
	return ret;
}

static void release_usb(struct usbwm_dev *udev)
{
	struct tx_cxt	*tx = &udev->tx;
	struct rx_cxt	*rx = &udev->rx;
	struct usb_tx	*t, *t_next;
	struct usb_rx	*r, *r_next;

	list_for_each_entry_safe(t, t_next, &tx->sdu_list, list) {
		list_del(&t->list);
		free_tx_struct(t);
	}

	list_for_each_entry_safe(t, t_next, &tx->hci_list, list) {
		list_del(&t->list);
		free_tx_struct(t);
	}

	list_for_each_entry_safe(t, t_next, &tx->free_list, list) {
		list_del(&t->list);
		free_tx_struct(t);
	}

	list_for_each_entry_safe(r, r_next, &rx->free_list, list) {
		list_del(&r->list);
		free_rx_struct(r);
	}

	list_for_each_entry_safe(r, r_next, &rx->used_list, list) {
		list_del(&r->list);
		free_rx_struct(r);
	}
}

static void gdm_usb_send_complete(struct urb *urb)
{
	struct usb_tx *t = urb->context;
	struct tx_cxt *tx = t->tx_cxt;
	u8 *pkt = t->buf;
	u16 cmd_evt;
	unsigned long flags;

	/* Completion by usb_unlink_urb */
	if (urb->status == -ECONNRESET)
		return;

	spin_lock_irqsave(&tx->lock, flags);

	if (t->callback)
		t->callback(t->cb_data);

	/* Delete from sdu list or hci list. */
	list_del(&t->list);

	cmd_evt = (pkt[0] << 8) | pkt[1];
	if (cmd_evt == WIMAX_TX_SDU)
		put_tx_struct(tx, t);
	else
		free_tx_struct(t);

	spin_unlock_irqrestore(&tx->lock, flags);
}

static int gdm_usb_send(void *priv_dev, void *data, int len,
			void (*cb)(void *data), void *cb_data)
{
	struct usbwm_dev *udev = priv_dev;
	struct usb_device *usbdev = udev->usbdev;
	struct tx_cxt *tx = &udev->tx;
	struct usb_tx *t;
	int padding = udev->padding;
	int no_spc = 0, ret;
	u8 *pkt = data;
	u16 cmd_evt;
	unsigned long flags;

	if (!udev->usbdev) {
		printk(KERN_ERR "%s: No such device\n", __func__);
		return -ENODEV;
	}

	BUG_ON(len > TX_BUF_SIZE - padding - 1);

	spin_lock_irqsave(&tx->lock, flags);

	cmd_evt = (pkt[0] << 8) | pkt[1];
	if (cmd_evt == WIMAX_TX_SDU) {
		t = get_tx_struct(tx, &no_spc);
		if (t == NULL) {
			/* This case must not happen. */
			spin_unlock_irqrestore(&tx->lock, flags);
			return -ENOSPC;
		}
		list_add_tail(&t->list, &tx->sdu_list);
	} else {
		t = alloc_tx_struct(tx);
		if (t == NULL) {
			spin_unlock_irqrestore(&tx->lock, flags);
			return -ENOMEM;
		}
		list_add_tail(&t->list, &tx->hci_list);
	}

	memcpy(t->buf + padding, data, len);
	t->callback = cb;
	t->cb_data = cb_data;

	/*
	 * In some cases, USB Module of WiMax is blocked when data size is
	 * the multiple of 512. So, increment length by one in that case.
	 */
	if ((len % 512) == 0)
		len++;

	usb_fill_bulk_urb(t->urb,
			usbdev,
			usb_sndbulkpipe(usbdev, 1),
			t->buf,
			len + padding,
			gdm_usb_send_complete,
			t);

#ifdef DEBUG
	hexdump("usb_send", t->buf, len + padding);
#endif
#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
	if (usbdev->state & USB_STATE_SUSPENDED) {
		list_add_tail(&t->p_list, &tx->pending_list);
		schedule_work(&udev->pm_ws);
		goto out;
	}
#endif /* CONFIG_WIMAX_GDM72XX_USB_PM */

#ifdef CONFIG_WIMAX_GDM72XX_K_MODE
	if (udev->bw_switch) {
		list_add_tail(&t->p_list, &tx->pending_list);
		goto out;
	} else if (cmd_evt == WIMAX_SCAN) {
		struct rx_cxt *rx;
		struct usb_rx *r;

		rx = &udev->rx;

		list_for_each_entry(r, &rx->used_list, list)
			usb_unlink_urb(r->urb);
		udev->bw_switch = 1;

		spin_lock(&k_lock);
		list_add_tail(&udev->list, &k_list);
		spin_unlock(&k_lock);

		wake_up(&k_wait);
	}
#endif /* CONFIG_WIMAX_GDM72XX_K_MODE */

	ret = usb_submit_urb(t->urb, GFP_ATOMIC);
	if (ret)
		goto send_fail;

#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
	usb_mark_last_busy(usbdev);
#endif /* CONFIG_WIMAX_GDM72XX_USB_PM */

#if defined(CONFIG_WIMAX_GDM72XX_USB_PM) || defined(CONFIG_WIMAX_GDM72XX_K_MODE)
out:
#endif
	spin_unlock_irqrestore(&tx->lock, flags);

	if (no_spc)
		return -ENOSPC;

	return 0;

send_fail:
	t->callback = NULL;
	gdm_usb_send_complete(t->urb);
	spin_unlock_irqrestore(&tx->lock, flags);
	return ret;
}

static void gdm_usb_rcv_complete(struct urb *urb)
{
	struct usb_rx *r = urb->context;
	struct rx_cxt *rx = r->rx_cxt;
	struct usbwm_dev *udev = container_of(r->rx_cxt, struct usbwm_dev, rx);
	struct tx_cxt *tx = &udev->tx;
	struct usb_tx *t;
	u16 cmd_evt;
	unsigned long flags;

#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
	struct usb_device *dev = urb->dev;
#endif

	/* Completion by usb_unlink_urb */
	if (urb->status == -ECONNRESET)
		return;

	spin_lock_irqsave(&tx->lock, flags);

	if (!urb->status) {
		cmd_evt = (r->buf[0] << 8) | (r->buf[1]);
#ifdef DEBUG
		hexdump("usb_receive", r->buf, urb->actual_length);
#endif
		if (cmd_evt == WIMAX_SDU_TX_FLOW) {
			if (r->buf[4] == 0) {
#ifdef DEBUG
				printk(KERN_DEBUG "WIMAX ==> STOP SDU TX\n");
#endif
				list_for_each_entry(t, &tx->sdu_list, list)
					usb_unlink_urb(t->urb);
			} else if (r->buf[4] == 1) {
#ifdef DEBUG
				printk(KERN_DEBUG "WIMAX ==> START SDU TX\n");
#endif
				list_for_each_entry(t, &tx->sdu_list, list) {
					usb_submit_urb(t->urb, GFP_ATOMIC);
				}
				/*
				 * If free buffer for sdu tx doesn't
				 * exist, then tx queue should not be
				 * woken. For this reason, don't pass
				 * the command, START_SDU_TX.
				 */
				if (list_empty(&tx->free_list))
					urb->actual_length = 0;
			}
		}
	}

	if (!urb->status && r->callback)
		r->callback(r->cb_data, r->buf, urb->actual_length);

	spin_lock(&rx->lock);
	put_rx_struct(rx, r);
	spin_unlock(&rx->lock);

	spin_unlock_irqrestore(&tx->lock, flags);

#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
	usb_mark_last_busy(dev);
#endif
}

static int gdm_usb_receive(void *priv_dev,
			void (*cb)(void *cb_data, void *data, int len),
			void *cb_data)
{
	struct usbwm_dev *udev = priv_dev;
	struct usb_device *usbdev = udev->usbdev;
	struct rx_cxt *rx = &udev->rx;
	struct usb_rx *r;
	unsigned long flags;

	if (!udev->usbdev) {
		printk(KERN_ERR "%s: No such device\n", __func__);
		return -ENODEV;
	}

	spin_lock_irqsave(&rx->lock, flags);
	r = get_rx_struct(rx);
	spin_unlock_irqrestore(&rx->lock, flags);

	if (r == NULL)
		return -ENOMEM;

	r->callback = cb;
	r->cb_data = cb_data;

	usb_fill_bulk_urb(r->urb,
			usbdev,
			usb_rcvbulkpipe(usbdev, 0x82),
			r->buf,
			RX_BUF_SIZE,
			gdm_usb_rcv_complete,
			r);

	return usb_submit_urb(r->urb, GFP_ATOMIC);
}

#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
static void do_pm_control(struct work_struct *work)
{
	struct usbwm_dev *udev = container_of(work, struct usbwm_dev, pm_ws);
	struct tx_cxt *tx = &udev->tx;
	int ret;
	unsigned long flags;

	ret = usb_autopm_get_interface(udev->intf);
	if (!ret)
		usb_autopm_put_interface(udev->intf);

	spin_lock_irqsave(&tx->lock, flags);
	if (!(udev->usbdev->state & USB_STATE_SUSPENDED)
		&& (!list_empty(&tx->hci_list) || !list_empty(&tx->sdu_list))) {
		struct usb_tx *t, *temp;

		list_for_each_entry_safe(t, temp, &tx->pending_list, p_list) {
			list_del(&t->p_list);
			ret =  usb_submit_urb(t->urb, GFP_ATOMIC);

			if (ret) {
				t->callback = NULL;
				gdm_usb_send_complete(t->urb);
			}
		}
	}
	spin_unlock_irqrestore(&tx->lock, flags);
}
#endif /* CONFIG_WIMAX_GDM72XX_USB_PM */

static int gdm_usb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	int ret = 0;
	u8 bConfigurationValue;
	struct phy_dev *phy_dev = NULL;
	struct usbwm_dev *udev = NULL;
	u16 idVendor, idProduct, bcdDevice;

	struct usb_device *usbdev = interface_to_usbdev(intf);

	usb_get_dev(usbdev);
	bConfigurationValue = usbdev->actconfig->desc.bConfigurationValue;

	/*USB description is set up with Little-Endian*/
	idVendor = L2H(usbdev->descriptor.idVendor);
	idProduct = L2H(usbdev->descriptor.idProduct);
	bcdDevice = L2H(usbdev->descriptor.bcdDevice);

	printk(KERN_INFO "Found GDM USB VID = 0x%04x PID = 0x%04x...\n",
		idVendor, idProduct);
	printk(KERN_INFO "GCT WiMax driver version %s\n", DRIVER_VERSION);


	if (idProduct == EMERGENCY_PID) {
		ret = usb_emergency(usbdev);
		goto out;
	}

	/* Support for EEPROM bootloader */
	if (bConfigurationValue == DOWNLOAD_CONF_VALUE ||
		idProduct & B_DOWNLOAD) {
		ret = usb_boot(usbdev, bcdDevice);
		goto out;
	}

	phy_dev = kmalloc(sizeof(*phy_dev), GFP_KERNEL);
	if (phy_dev == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	udev = kmalloc(sizeof(*udev), GFP_KERNEL);
	if (udev == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	memset(phy_dev, 0, sizeof(*phy_dev));
	memset(udev, 0, sizeof(*udev));

	if (idProduct == 0x7205 || idProduct == 0x7206)
		udev->padding = GDM7205_PADDING;
	else
		udev->padding = 0;

	phy_dev->priv_dev = (void *)udev;
	phy_dev->send_func = gdm_usb_send;
	phy_dev->rcv_func = gdm_usb_receive;

	ret = init_usb(udev);
	if (ret < 0)
		goto out;

	udev->usbdev = usbdev;

#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
	udev->intf = intf;

	intf->needs_remote_wakeup = 1;
	device_init_wakeup(&intf->dev, 1);

	pm_runtime_set_autosuspend_delay(&usbdev->dev, 10 * 1000); /* msec */

	INIT_WORK(&udev->pm_ws, do_pm_control);
#endif /* CONFIG_WIMAX_GDM72XX_USB_PM */

	ret = register_wimax_device(phy_dev);

out:
	if (ret) {
		kfree(phy_dev);
		kfree(udev);
	}
	usb_set_intfdata(intf, phy_dev);
	return ret;
}

static void gdm_usb_disconnect(struct usb_interface *intf)
{
	u8 bConfigurationValue;
	struct phy_dev *phy_dev;
	struct usbwm_dev *udev;
	u16 idProduct;
	struct usb_device *usbdev = interface_to_usbdev(intf);

	bConfigurationValue = usbdev->actconfig->desc.bConfigurationValue;
	phy_dev = usb_get_intfdata(intf);

	/*USB description is set up with Little-Endian*/
	idProduct = L2H(usbdev->descriptor.idProduct);

	if (idProduct != EMERGENCY_PID &&
			bConfigurationValue != DOWNLOAD_CONF_VALUE &&
			(idProduct & B_DOWNLOAD) == 0) {
		udev = phy_dev->priv_dev;
		udev->usbdev = NULL;

		unregister_wimax_device(phy_dev);
		release_usb(udev);
		kfree(udev);
		kfree(phy_dev);
	}

	usb_put_dev(usbdev);
}

#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
static int gdm_suspend(struct usb_interface *intf, pm_message_t pm_msg)
{
	struct phy_dev *phy_dev;
	struct usbwm_dev *udev;
	struct rx_cxt *rx;
	struct usb_rx *r;

	phy_dev = usb_get_intfdata(intf);
	udev = phy_dev->priv_dev;
	rx = &udev->rx;

	list_for_each_entry(r, &rx->used_list, list)
		usb_unlink_urb(r->urb);

	return 0;
}

static int gdm_resume(struct usb_interface *intf)
{
	struct phy_dev *phy_dev;
	struct usbwm_dev *udev;
	struct rx_cxt *rx;
	struct usb_rx *r;

	phy_dev = usb_get_intfdata(intf);
	udev = phy_dev->priv_dev;
	rx = &udev->rx;

	list_for_each_entry(r, &rx->used_list, list)
		usb_submit_urb(r->urb, GFP_ATOMIC);

	return 0;
}

#endif /* CONFIG_WIMAX_GDM72XX_USB_PM */

#ifdef CONFIG_WIMAX_GDM72XX_K_MODE
static int k_mode_thread(void *arg)
{
	struct usbwm_dev *udev;
	struct tx_cxt *tx;
	struct rx_cxt *rx;
	struct usb_tx *t, *temp;
	struct usb_rx *r;
	unsigned long flags, flags2, expire;
	int ret;

	daemonize("k_mode_wimax");

	while (!k_mode_stop) {

		spin_lock_irqsave(&k_lock, flags2);
		while (!list_empty(&k_list)) {

			udev = list_entry(k_list.next, struct usbwm_dev, list);
			tx = &udev->tx;
			rx = &udev->rx;

			list_del(&udev->list);
			spin_unlock_irqrestore(&k_lock, flags2);

			expire = jiffies + K_WAIT_TIME;
			while (jiffies < expire)
				schedule_timeout(K_WAIT_TIME);

			list_for_each_entry(r, &rx->used_list, list)
				usb_submit_urb(r->urb, GFP_ATOMIC);

			spin_lock_irqsave(&tx->lock, flags);

			list_for_each_entry_safe(t, temp, &tx->pending_list,
						p_list) {
				list_del(&t->p_list);
				ret = usb_submit_urb(t->urb, GFP_ATOMIC);

				if (ret) {
					t->callback = NULL;
					gdm_usb_send_complete(t->urb);
				}
			}

			udev->bw_switch = 0;
			spin_unlock_irqrestore(&tx->lock, flags);

			spin_lock_irqsave(&k_lock, flags2);
		}
		spin_unlock_irqrestore(&k_lock, flags2);

		interruptible_sleep_on(&k_wait);
	}
	return 0;
}
#endif /* CONFIG_WIMAX_GDM72XX_K_MODE */

static struct usb_driver gdm_usb_driver = {
	.name = "gdm_wimax",
	.probe = gdm_usb_probe,
	.disconnect = gdm_usb_disconnect,
	.id_table = id_table,
#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
	.supports_autosuspend = 1,
	.suspend = gdm_suspend,
	.resume = gdm_resume,
	.reset_resume = gdm_resume,
#endif
};

static int __init usb_gdm_wimax_init(void)
{
#ifdef CONFIG_WIMAX_GDM72XX_K_MODE
	kernel_thread(k_mode_thread, NULL, CLONE_KERNEL);
#endif /* CONFIG_WIMAX_GDM72XX_K_MODE */
	return usb_register(&gdm_usb_driver);
}

static void __exit usb_gdm_wimax_exit(void)
{
#ifdef CONFIG_WIMAX_GDM72XX_K_MODE
	k_mode_stop = 1;
	wake_up(&k_wait);
#endif
	usb_deregister(&gdm_usb_driver);
}

module_init(usb_gdm_wimax_init);
module_exit(usb_gdm_wimax_exit);

MODULE_VERSION(DRIVER_VERSION);
MODULE_DESCRIPTION("GCT WiMax Device Driver");
MODULE_AUTHOR("Ethan Park");
MODULE_LICENSE("GPL");
