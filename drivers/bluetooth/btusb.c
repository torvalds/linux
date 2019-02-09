/*
 *
 *  Generic Bluetooth USB driver
 *
 *  Copyright (C) 2005-2008  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btintel.h"
#include "btbcm.h"
#include "btrtl.h"

#define VERSION "0.8"

static bool disable_scofix;
static bool force_scofix;

static bool reset = true;

static struct usb_driver btusb_driver;

#define BTUSB_IGNORE		0x01
#define BTUSB_DIGIANSWER	0x02
#define BTUSB_CSR		0x04
#define BTUSB_SNIFFER		0x08
#define BTUSB_BCM92035		0x10
#define BTUSB_BROKEN_ISOC	0x20
#define BTUSB_WRONG_SCO_MTU	0x40
#define BTUSB_ATH3012		0x80
#define BTUSB_INTEL		0x100
#define BTUSB_INTEL_BOOT	0x200
#define BTUSB_BCM_PATCHRAM	0x400
#define BTUSB_MARVELL		0x800
#define BTUSB_SWAVE		0x1000
#define BTUSB_INTEL_NEW		0x2000
#define BTUSB_AMP		0x4000
#define BTUSB_QCA_ROME		0x8000
#define BTUSB_BCM_APPLE		0x10000
#define BTUSB_REALTEK		0x20000
#define BTUSB_BCM2045		0x40000
#define BTUSB_IFNUM_2		0x80000

static const struct usb_device_id btusb_table[] = {
	/* Generic Bluetooth USB device */
	{ USB_DEVICE_INFO(0xe0, 0x01, 0x01) },

	/* Generic Bluetooth AMP device */
	{ USB_DEVICE_INFO(0xe0, 0x01, 0x04), .driver_info = BTUSB_AMP },

	/* Generic Bluetooth USB interface */
	{ USB_INTERFACE_INFO(0xe0, 0x01, 0x01) },

	/* Apple-specific (Broadcom) devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x05ac, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_APPLE | BTUSB_IFNUM_2 },

	/* MediaTek MT76x0E */
	{ USB_DEVICE(0x0e8d, 0x763f) },

	/* Broadcom SoftSailing reporting vendor specific */
	{ USB_DEVICE(0x0a5c, 0x21e1) },

	/* Apple MacBookPro 7,1 */
	{ USB_DEVICE(0x05ac, 0x8213) },

	/* Apple iMac11,1 */
	{ USB_DEVICE(0x05ac, 0x8215) },

	/* Apple MacBookPro6,2 */
	{ USB_DEVICE(0x05ac, 0x8218) },

	/* Apple MacBookAir3,1, MacBookAir3,2 */
	{ USB_DEVICE(0x05ac, 0x821b) },

	/* Apple MacBookAir4,1 */
	{ USB_DEVICE(0x05ac, 0x821f) },

	/* Apple MacBookPro8,2 */
	{ USB_DEVICE(0x05ac, 0x821a) },

	/* Apple MacMini5,1 */
	{ USB_DEVICE(0x05ac, 0x8281) },

	/* AVM BlueFRITZ! USB v2.0 */
	{ USB_DEVICE(0x057c, 0x3800), .driver_info = BTUSB_SWAVE },

	/* Bluetooth Ultraport Module from IBM */
	{ USB_DEVICE(0x04bf, 0x030a) },

	/* ALPS Modules with non-standard id */
	{ USB_DEVICE(0x044e, 0x3001) },
	{ USB_DEVICE(0x044e, 0x3002) },

	/* Ericsson with non-standard id */
	{ USB_DEVICE(0x0bdb, 0x1002) },

	/* Canyon CN-BTU1 with HID interfaces */
	{ USB_DEVICE(0x0c10, 0x0000) },

	/* Broadcom BCM20702A0 */
	{ USB_DEVICE(0x413c, 0x8197) },

	/* Broadcom BCM20702B0 (Dynex/Insignia) */
	{ USB_DEVICE(0x19ff, 0x0239), .driver_info = BTUSB_BCM_PATCHRAM },

	/* Broadcom BCM43142A0 (Foxconn/Lenovo) */
	{ USB_DEVICE(0x105b, 0xe065), .driver_info = BTUSB_BCM_PATCHRAM },

	/* Foxconn - Hon Hai */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0489, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Lite-On Technology - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x04ca, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Broadcom devices with vendor specific id */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0a5c, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* ASUSTek Computer - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0b05, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Belkin F8065bf - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x050d, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* IMC Networks - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x13d3, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Intel Bluetooth USB Bootloader (RAM module) */
	{ USB_DEVICE(0x8087, 0x0a5a),
	  .driver_info = BTUSB_INTEL_BOOT | BTUSB_BROKEN_ISOC },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, btusb_table);

static const struct usb_device_id blacklist_table[] = {
	/* CSR BlueCore devices */
	{ USB_DEVICE(0x0a12, 0x0001), .driver_info = BTUSB_CSR },

	/* Broadcom BCM2033 without firmware */
	{ USB_DEVICE(0x0a5c, 0x2033), .driver_info = BTUSB_IGNORE },

	/* Broadcom BCM2045 devices */
	{ USB_DEVICE(0x0a5c, 0x2045), .driver_info = BTUSB_BCM2045 },

	/* Atheros 3011 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe027), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0489, 0xe03d), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x04f2, 0xaff1), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0930, 0x0215), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0cf3, 0x3002), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0cf3, 0xe019), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x13d3, 0x3304), .driver_info = BTUSB_IGNORE },

	/* Atheros AR9285 Malbec with sflash firmware */
	{ USB_DEVICE(0x03f0, 0x311d), .driver_info = BTUSB_IGNORE },

	/* Atheros 3012 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe04d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe04e), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe056), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe057), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe05f), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe076), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe078), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04c5, 0x1330), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3005), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3006), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3007), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3008), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x300b), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x300d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x300f), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3010), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0219), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x021c), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0220), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0227), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0b05, 0x17d0), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x0036), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3008), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311e), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311f), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3121), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x817a), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x817b), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe003), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe005), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe006), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3362), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3375), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3393), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3402), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3408), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3423), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3432), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3474), .driver_info = BTUSB_ATH3012 },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe02c), .driver_info = BTUSB_IGNORE },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe036), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe03c), .driver_info = BTUSB_ATH3012 },

	/* QCA ROME chipset */
	{ USB_DEVICE(0x0cf3, 0xe007), .driver_info = BTUSB_QCA_ROME },
	{ USB_DEVICE(0x0cf3, 0xe300), .driver_info = BTUSB_QCA_ROME },
	{ USB_DEVICE(0x0cf3, 0xe360), .driver_info = BTUSB_QCA_ROME },

	/* Broadcom BCM2035 */
	{ USB_DEVICE(0x0a5c, 0x2009), .driver_info = BTUSB_BCM92035 },
	{ USB_DEVICE(0x0a5c, 0x200a), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2035), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Broadcom BCM2045 */
	{ USB_DEVICE(0x0a5c, 0x2039), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2101), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* IBM/Lenovo ThinkPad with Broadcom chip */
	{ USB_DEVICE(0x0a5c, 0x201e), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2110), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* HP laptop with Broadcom chip */
	{ USB_DEVICE(0x03f0, 0x171d), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell laptop with Broadcom chip */
	{ USB_DEVICE(0x413c, 0x8126), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell Wireless 370 and 410 devices */
	{ USB_DEVICE(0x413c, 0x8152), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x413c, 0x8156), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Belkin F8T012 and F8T013 devices */
	{ USB_DEVICE(0x050d, 0x0012), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x050d, 0x0013), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Asus WL-BTD202 device */
	{ USB_DEVICE(0x0b05, 0x1715), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Kensington Bluetooth USB adapter */
	{ USB_DEVICE(0x047d, 0x105e), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* RTX Telecom based adapters with buggy SCO support */
	{ USB_DEVICE(0x0400, 0x0807), .driver_info = BTUSB_BROKEN_ISOC },
	{ USB_DEVICE(0x0400, 0x080a), .driver_info = BTUSB_BROKEN_ISOC },

	/* CONWISE Technology based adapters with buggy SCO support */
	{ USB_DEVICE(0x0e5e, 0x6622), .driver_info = BTUSB_BROKEN_ISOC },

	/* Roper Class 1 Bluetooth Dongle (Silicon Wave based) */
	{ USB_DEVICE(0x1310, 0x0001), .driver_info = BTUSB_SWAVE },

	/* Digianswer devices */
	{ USB_DEVICE(0x08fd, 0x0001), .driver_info = BTUSB_DIGIANSWER },
	{ USB_DEVICE(0x08fd, 0x0002), .driver_info = BTUSB_IGNORE },

	/* CSR BlueCore Bluetooth Sniffer */
	{ USB_DEVICE(0x0a12, 0x0002),
	  .driver_info = BTUSB_SNIFFER | BTUSB_BROKEN_ISOC },

	/* Frontline ComProbe Bluetooth Sniffer */
	{ USB_DEVICE(0x16d3, 0x0002),
	  .driver_info = BTUSB_SNIFFER | BTUSB_BROKEN_ISOC },

	/* Marvell Bluetooth devices */
	{ USB_DEVICE(0x1286, 0x2044), .driver_info = BTUSB_MARVELL },
	{ USB_DEVICE(0x1286, 0x2046), .driver_info = BTUSB_MARVELL },

	/* Intel Bluetooth devices */
	{ USB_DEVICE(0x8087, 0x07da), .driver_info = BTUSB_CSR },
	{ USB_DEVICE(0x8087, 0x07dc), .driver_info = BTUSB_INTEL },
	{ USB_DEVICE(0x8087, 0x0a2a), .driver_info = BTUSB_INTEL },
	{ USB_DEVICE(0x8087, 0x0a2b), .driver_info = BTUSB_INTEL_NEW },

	/* Other Intel Bluetooth devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x8087, 0xe0, 0x01, 0x01),
	  .driver_info = BTUSB_IGNORE },

	/* Realtek Bluetooth devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0bda, 0xe0, 0x01, 0x01),
	  .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723AE Bluetooth devices */
	{ USB_DEVICE(0x0930, 0x021d), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3394), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723BE Bluetooth devices */
	{ USB_DEVICE(0x0489, 0xe085), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x0489, 0xe08b), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3410), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3416), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3459), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8821AE Bluetooth devices */
	{ USB_DEVICE(0x0b05, 0x17dc), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3414), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3458), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3461), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3462), .driver_info = BTUSB_REALTEK },

	/* Silicon Wave based devices */
	{ USB_DEVICE(0x0c10, 0x0000), .driver_info = BTUSB_SWAVE },

	{ }	/* Terminating entry */
};

#define BTUSB_MAX_ISOC_FRAMES	10

#define BTUSB_INTR_RUNNING	0
#define BTUSB_BULK_RUNNING	1
#define BTUSB_ISOC_RUNNING	2
#define BTUSB_SUSPENDING	3
#define BTUSB_DID_ISO_RESUME	4
#define BTUSB_BOOTLOADER	5
#define BTUSB_DOWNLOADING	6
#define BTUSB_FIRMWARE_LOADED	7
#define BTUSB_FIRMWARE_FAILED	8
#define BTUSB_BOOTING		9
#define BTUSB_RESET_RESUME	10
#define BTUSB_DIAG_RUNNING	11

struct btusb_data {
	struct hci_dev       *hdev;
	struct usb_device    *udev;
	struct usb_interface *intf;
	struct usb_interface *isoc;
	struct usb_interface *diag;

	unsigned long flags;

	struct work_struct work;
	struct work_struct waker;

	struct usb_anchor deferred;
	struct usb_anchor tx_anchor;
	int tx_in_flight;
	spinlock_t txlock;

	struct usb_anchor intr_anchor;
	struct usb_anchor bulk_anchor;
	struct usb_anchor isoc_anchor;
	struct usb_anchor diag_anchor;
	spinlock_t rxlock;

	struct sk_buff *evt_skb;
	struct sk_buff *acl_skb;
	struct sk_buff *sco_skb;

	struct usb_endpoint_descriptor *intr_ep;
	struct usb_endpoint_descriptor *bulk_tx_ep;
	struct usb_endpoint_descriptor *bulk_rx_ep;
	struct usb_endpoint_descriptor *isoc_tx_ep;
	struct usb_endpoint_descriptor *isoc_rx_ep;
	struct usb_endpoint_descriptor *diag_tx_ep;
	struct usb_endpoint_descriptor *diag_rx_ep;

	__u8 cmdreq_type;
	__u8 cmdreq;

	unsigned int sco_num;
	int isoc_altsetting;
	int suspend_count;

	int (*recv_event)(struct hci_dev *hdev, struct sk_buff *skb);
	int (*recv_bulk)(struct btusb_data *data, void *buffer, int count);

	int (*setup_on_usb)(struct hci_dev *hdev);
};

static inline void btusb_free_frags(struct btusb_data *data)
{
	unsigned long flags;

	spin_lock_irqsave(&data->rxlock, flags);

	kfree_skb(data->evt_skb);
	data->evt_skb = NULL;

	kfree_skb(data->acl_skb);
	data->acl_skb = NULL;

	kfree_skb(data->sco_skb);
	data->sco_skb = NULL;

	spin_unlock_irqrestore(&data->rxlock, flags);
}

static int btusb_recv_intr(struct btusb_data *data, void *buffer, int count)
{
	struct sk_buff *skb;
	int err = 0;

	spin_lock(&data->rxlock);
	skb = data->evt_skb;

	while (count) {
		int len;

		if (!skb) {
			skb = bt_skb_alloc(HCI_MAX_EVENT_SIZE, GFP_ATOMIC);
			if (!skb) {
				err = -ENOMEM;
				break;
			}

			bt_cb(skb)->pkt_type = HCI_EVENT_PKT;
			bt_cb(skb)->expect = HCI_EVENT_HDR_SIZE;
		}

		len = min_t(uint, bt_cb(skb)->expect, count);
		memcpy(skb_put(skb, len), buffer, len);

		count -= len;
		buffer += len;
		bt_cb(skb)->expect -= len;

		if (skb->len == HCI_EVENT_HDR_SIZE) {
			/* Complete event header */
			bt_cb(skb)->expect = hci_event_hdr(skb)->plen;

			if (skb_tailroom(skb) < bt_cb(skb)->expect) {
				kfree_skb(skb);
				skb = NULL;

				err = -EILSEQ;
				break;
			}
		}

		if (bt_cb(skb)->expect == 0) {
			/* Complete frame */
			data->recv_event(data->hdev, skb);
			skb = NULL;
		}
	}

	data->evt_skb = skb;
	spin_unlock(&data->rxlock);

	return err;
}

static int btusb_recv_bulk(struct btusb_data *data, void *buffer, int count)
{
	struct sk_buff *skb;
	int err = 0;

	spin_lock(&data->rxlock);
	skb = data->acl_skb;

	while (count) {
		int len;

		if (!skb) {
			skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
			if (!skb) {
				err = -ENOMEM;
				break;
			}

			bt_cb(skb)->pkt_type = HCI_ACLDATA_PKT;
			bt_cb(skb)->expect = HCI_ACL_HDR_SIZE;
		}

		len = min_t(uint, bt_cb(skb)->expect, count);
		memcpy(skb_put(skb, len), buffer, len);

		count -= len;
		buffer += len;
		bt_cb(skb)->expect -= len;

		if (skb->len == HCI_ACL_HDR_SIZE) {
			__le16 dlen = hci_acl_hdr(skb)->dlen;

			/* Complete ACL header */
			bt_cb(skb)->expect = __le16_to_cpu(dlen);

			if (skb_tailroom(skb) < bt_cb(skb)->expect) {
				kfree_skb(skb);
				skb = NULL;

				err = -EILSEQ;
				break;
			}
		}

		if (bt_cb(skb)->expect == 0) {
			/* Complete frame */
			hci_recv_frame(data->hdev, skb);
			skb = NULL;
		}
	}

	data->acl_skb = skb;
	spin_unlock(&data->rxlock);

	return err;
}

static int btusb_recv_isoc(struct btusb_data *data, void *buffer, int count)
{
	struct sk_buff *skb;
	int err = 0;

	spin_lock(&data->rxlock);
	skb = data->sco_skb;

	while (count) {
		int len;

		if (!skb) {
			skb = bt_skb_alloc(HCI_MAX_SCO_SIZE, GFP_ATOMIC);
			if (!skb) {
				err = -ENOMEM;
				break;
			}

			bt_cb(skb)->pkt_type = HCI_SCODATA_PKT;
			bt_cb(skb)->expect = HCI_SCO_HDR_SIZE;
		}

		len = min_t(uint, bt_cb(skb)->expect, count);
		memcpy(skb_put(skb, len), buffer, len);

		count -= len;
		buffer += len;
		bt_cb(skb)->expect -= len;

		if (skb->len == HCI_SCO_HDR_SIZE) {
			/* Complete SCO header */
			bt_cb(skb)->expect = hci_sco_hdr(skb)->dlen;

			if (skb_tailroom(skb) < bt_cb(skb)->expect) {
				kfree_skb(skb);
				skb = NULL;

				err = -EILSEQ;
				break;
			}
		}

		if (bt_cb(skb)->expect == 0) {
			/* Complete frame */
			hci_recv_frame(data->hdev, skb);
			skb = NULL;
		}
	}

	data->sco_skb = skb;
	spin_unlock(&data->rxlock);

	return err;
}

static void btusb_intr_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (btusb_recv_intr(data, urb->transfer_buffer,
				    urb->actual_length) < 0) {
			BT_ERR("%s corrupted event packet", hdev->name);
			hdev->stat.err_rx++;
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_INTR_RUNNING, &data->flags))
		return;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_intr_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	if (!data->intr_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->intr_ep->wMaxPacketSize);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(data->udev, data->intr_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size,
			 btusb_intr_complete, hdev, data->intr_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_bulk_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (data->recv_bulk(data, urb->transfer_buffer,
				    urb->actual_length) < 0) {
			BT_ERR("%s corrupted ACL packet", hdev->name);
			hdev->stat.err_rx++;
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_BULK_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->bulk_anchor);
	usb_mark_last_busy(data->udev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_bulk_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = HCI_MAX_FRAME_SIZE;

	BT_DBG("%s", hdev->name);

	if (!data->bulk_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(data->udev, data->bulk_rx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe, buf, size,
			  btusb_bulk_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->bulk_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_isoc_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int i, err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		for (i = 0; i < urb->number_of_packets; i++) {
			unsigned int offset = urb->iso_frame_desc[i].offset;
			unsigned int length = urb->iso_frame_desc[i].actual_length;

			if (urb->iso_frame_desc[i].status)
				continue;

			hdev->stat.byte_rx += length;

			if (btusb_recv_isoc(data, urb->transfer_buffer + offset,
					    length) < 0) {
				BT_ERR("%s corrupted SCO packet", hdev->name);
				hdev->stat.err_rx++;
			}
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_ISOC_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline void __fill_isoc_descriptor(struct urb *urb, int len, int mtu)
{
	int i, offset = 0;

	BT_DBG("len %d mtu %d", len, mtu);

	for (i = 0; i < BTUSB_MAX_ISOC_FRAMES && len >= mtu;
					i++, offset += mtu, len -= mtu) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = mtu;
	}

	if (len && i < BTUSB_MAX_ISOC_FRAMES) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = len;
		i++;
	}

	urb->number_of_packets = i;
}

static int btusb_submit_isoc_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	if (!data->isoc_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize) *
						BTUSB_MAX_ISOC_FRAMES;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvisocpipe(data->udev, data->isoc_rx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size, btusb_isoc_complete,
			 hdev, data->isoc_rx_ep->bInterval);

	urb->transfer_flags = URB_FREE_BUFFER | URB_ISO_ASAP;

	__fill_isoc_descriptor(urb, size,
			       le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize));

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_diag_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (urb->status == 0) {
		struct sk_buff *skb;

		skb = bt_skb_alloc(urb->actual_length, GFP_ATOMIC);
		if (skb) {
			memcpy(skb_put(skb, urb->actual_length),
			       urb->transfer_buffer, urb->actual_length);
			hci_recv_diag(hdev, skb);
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_DIAG_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->diag_anchor);
	usb_mark_last_busy(data->udev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_diag_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = HCI_MAX_FRAME_SIZE;

	BT_DBG("%s", hdev->name);

	if (!data->diag_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(data->udev, data->diag_rx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe, buf, size,
			  btusb_diag_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->diag_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
			       hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;
	struct btusb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	spin_lock(&data->txlock);
	data->tx_in_flight--;
	spin_unlock(&data->txlock);

	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static void btusb_isoc_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static int btusb_open(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s", hdev->name);

	/* Patching USB firmware files prior to starting any URBs of HCI path
	 * It is more safe to use USB bulk channel for downloading USB patch
	 */
	if (data->setup_on_usb) {
		err = data->setup_on_usb(hdev);
		if (err < 0)
			return err;
	}

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return err;

	data->intf->needs_remote_wakeup = 1;

	if (test_and_set_bit(BTUSB_INTR_RUNNING, &data->flags))
		goto done;

	err = btusb_submit_intr_urb(hdev, GFP_KERNEL);
	if (err < 0)
		goto failed;

	err = btusb_submit_bulk_urb(hdev, GFP_KERNEL);
	if (err < 0) {
		usb_kill_anchored_urbs(&data->intr_anchor);
		goto failed;
	}

	set_bit(BTUSB_BULK_RUNNING, &data->flags);
	btusb_submit_bulk_urb(hdev, GFP_KERNEL);

	if (data->diag) {
		if (!btusb_submit_diag_urb(hdev, GFP_KERNEL))
			set_bit(BTUSB_DIAG_RUNNING, &data->flags);
	}

done:
	usb_autopm_put_interface(data->intf);
	return 0;

failed:
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);
	usb_autopm_put_interface(data->intf);
	return err;
}

static void btusb_stop_traffic(struct btusb_data *data)
{
	usb_kill_anchored_urbs(&data->intr_anchor);
	usb_kill_anchored_urbs(&data->bulk_anchor);
	usb_kill_anchored_urbs(&data->isoc_anchor);
	usb_kill_anchored_urbs(&data->diag_anchor);
}

static int btusb_close(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s", hdev->name);

	cancel_work_sync(&data->work);
	cancel_work_sync(&data->waker);

	clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
	clear_bit(BTUSB_BULK_RUNNING, &data->flags);
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);
	clear_bit(BTUSB_DIAG_RUNNING, &data->flags);

	btusb_stop_traffic(data);
	btusb_free_frags(data);

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		goto failed;

	data->intf->needs_remote_wakeup = 0;
	usb_autopm_put_interface(data->intf);

failed:
	usb_scuttle_anchored_urbs(&data->deferred);
	return 0;
}

static int btusb_flush(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s", hdev->name);

	usb_kill_anchored_urbs(&data->tx_anchor);
	btusb_free_frags(data);

	return 0;
}

static struct urb *alloc_ctrl_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr) {
		usb_free_urb(urb);
		return ERR_PTR(-ENOMEM);
	}

	dr->bRequestType = data->cmdreq_type;
	dr->bRequest     = data->cmdreq;
	dr->wIndex       = 0;
	dr->wValue       = 0;
	dr->wLength      = __cpu_to_le16(skb->len);

	pipe = usb_sndctrlpipe(data->udev, 0x00);

	usb_fill_control_urb(urb, data->udev, pipe, (void *)dr,
			     skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static struct urb *alloc_bulk_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned int pipe;

	if (!data->bulk_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	pipe = usb_sndbulkpipe(data->udev, data->bulk_tx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe,
			  skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static struct urb *alloc_isoc_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned int pipe;

	if (!data->isoc_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	pipe = usb_sndisocpipe(data->udev, data->isoc_tx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe,
			 skb->data, skb->len, btusb_isoc_tx_complete,
			 skb, data->isoc_tx_ep->bInterval);

	urb->transfer_flags  = URB_ISO_ASAP;

	__fill_isoc_descriptor(urb, skb->len,
			       le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize));

	skb->dev = (void *)hdev;

	return urb;
}

static int submit_tx_urb(struct hci_dev *hdev, struct urb *urb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	usb_anchor_urb(urb, &data->tx_anchor);

	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
			       hdev->name, urb, -err);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	} else {
		usb_mark_last_busy(data->udev);
	}

	usb_free_urb(urb);
	return err;
}

static int submit_or_queue_tx_urb(struct hci_dev *hdev, struct urb *urb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	unsigned long flags;
	bool suspending;

	spin_lock_irqsave(&data->txlock, flags);
	suspending = test_bit(BTUSB_SUSPENDING, &data->flags);
	if (!suspending)
		data->tx_in_flight++;
	spin_unlock_irqrestore(&data->txlock, flags);

	if (!suspending)
		return submit_tx_urb(hdev, urb);

	usb_anchor_urb(urb, &data->deferred);
	schedule_work(&data->waker);

	usb_free_urb(urb);
	return 0;
}

static int btusb_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct urb *urb;

	BT_DBG("%s", hdev->name);

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		urb = alloc_ctrl_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.cmd_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_ACLDATA_PKT:
		urb = alloc_bulk_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.acl_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_SCODATA_PKT:
		if (hci_conn_num(hdev, SCO_LINK) < 1)
			return -ENODEV;

		urb = alloc_isoc_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.sco_tx++;
		return submit_tx_urb(hdev, urb);
	}

	return -EILSEQ;
}

static void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
	struct btusb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s evt %d", hdev->name, evt);

	if (hci_conn_num(hdev, SCO_LINK) != data->sco_num) {
		data->sco_num = hci_conn_num(hdev, SCO_LINK);
		schedule_work(&data->work);
	}
}

static inline int __set_isoc_interface(struct hci_dev *hdev, int altsetting)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_interface *intf = data->isoc;
	struct usb_endpoint_descriptor *ep_desc;
	int i, err;

	if (!data->isoc)
		return -ENODEV;

	err = usb_set_interface(data->udev, 1, altsetting);
	if (err < 0) {
		BT_ERR("%s setting interface failed (%d)", hdev->name, -err);
		return err;
	}

	data->isoc_altsetting = altsetting;

	data->isoc_tx_ep = NULL;
	data->isoc_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->isoc_tx_ep && usb_endpoint_is_isoc_out(ep_desc)) {
			data->isoc_tx_ep = ep_desc;
			continue;
		}

		if (!data->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)) {
			data->isoc_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->isoc_tx_ep || !data->isoc_rx_ep) {
		BT_ERR("%s invalid SCO descriptors", hdev->name);
		return -ENODEV;
	}

	return 0;
}

static void btusb_work(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, work);
	struct hci_dev *hdev = data->hdev;
	int new_alts;
	int err;

	if (data->sco_num > 0) {
		if (!test_bit(BTUSB_DID_ISO_RESUME, &data->flags)) {
			err = usb_autopm_get_interface(data->isoc ? data->isoc : data->intf);
			if (err < 0) {
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
				usb_kill_anchored_urbs(&data->isoc_anchor);
				return;
			}

			set_bit(BTUSB_DID_ISO_RESUME, &data->flags);
		}

		if (hdev->voice_setting & 0x0020) {
			static const int alts[3] = { 2, 4, 5 };

			new_alts = alts[data->sco_num - 1];
		} else {
			new_alts = data->sco_num;
		}

		if (data->isoc_altsetting != new_alts) {
			unsigned long flags;

			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			usb_kill_anchored_urbs(&data->isoc_anchor);

			/* When isochronous alternate setting needs to be
			 * changed, because SCO connection has been added
			 * or removed, a packet fragment may be left in the
			 * reassembling state. This could lead to wrongly
			 * assembled fragments.
			 *
			 * Clear outstanding fragment when selecting a new
			 * alternate setting.
			 */
			spin_lock_irqsave(&data->rxlock, flags);
			kfree_skb(data->sco_skb);
			data->sco_skb = NULL;
			spin_unlock_irqrestore(&data->rxlock, flags);

			if (__set_isoc_interface(hdev, new_alts) < 0)
				return;
		}

		if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
			if (btusb_submit_isoc_urb(hdev, GFP_KERNEL) < 0)
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			else
				btusb_submit_isoc_urb(hdev, GFP_KERNEL);
		}
	} else {
		clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		usb_kill_anchored_urbs(&data->isoc_anchor);

		__set_isoc_interface(hdev, 0);
		if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &data->flags))
			usb_autopm_put_interface(data->isoc ? data->isoc : data->intf);
	}
}

static void btusb_waker(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, waker);
	int err;

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return;

	usb_autopm_put_interface(data->intf);
}

static int btusb_setup_bcm92035(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	u8 val = 0x00;

	BT_DBG("%s", hdev->name);

	skb = __hci_cmd_sync(hdev, 0xfc3b, 1, &val, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		BT_ERR("BCM92035 command failed (%ld)", -PTR_ERR(skb));
	else
		kfree_skb(skb);

	return 0;
}

static int btusb_setup_csr(struct hci_dev *hdev)
{
	struct hci_rp_read_local_version *rp;
	struct sk_buff *skb;

	BT_DBG("%s", hdev->name);

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_VERSION, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);
		BT_ERR("%s: CSR: Local version failed (%d)", hdev->name, err);
		return err;
	}

	if (skb->len != sizeof(struct hci_rp_read_local_version)) {
		BT_ERR("%s: CSR: Local version length mismatch", hdev->name);
		kfree_skb(skb);
		return -EIO;
	}

	rp = (struct hci_rp_read_local_version *)skb->data;

	/* Detect controllers which aren't real CSR ones. */
	if (le16_to_cpu(rp->manufacturer) != 10 ||
	    le16_to_cpu(rp->lmp_subver) == 0x0c5c) {
		/* Clear the reset quirk since this is not an actual
		 * early Bluetooth 1.1 device from CSR.
		 */
		clear_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);

		/* These fake CSR controllers have all a broken
		 * stored link key handling and so just disable it.
		 */
		set_bit(HCI_QUIRK_BROKEN_STORED_LINK_KEY, &hdev->quirks);
	}

	kfree_skb(skb);

	return 0;
}

static const struct firmware *btusb_setup_intel_get_fw(struct hci_dev *hdev,
						       struct intel_version *ver)
{
	const struct firmware *fw;
	char fwname[64];
	int ret;

	snprintf(fwname, sizeof(fwname),
		 "intel/ibt-hw-%x.%x.%x-fw-%x.%x.%x.%x.%x.bseq",
		 ver->hw_platform, ver->hw_variant, ver->hw_revision,
		 ver->fw_variant,  ver->fw_revision, ver->fw_build_num,
		 ver->fw_build_ww, ver->fw_build_yy);

	ret = request_firmware(&fw, fwname, &hdev->dev);
	if (ret < 0) {
		if (ret == -EINVAL) {
			BT_ERR("%s Intel firmware file request failed (%d)",
			       hdev->name, ret);
			return NULL;
		}

		BT_ERR("%s failed to open Intel firmware file: %s(%d)",
		       hdev->name, fwname, ret);

		/* If the correct firmware patch file is not found, use the
		 * default firmware patch file instead
		 */
		snprintf(fwname, sizeof(fwname), "intel/ibt-hw-%x.%x.bseq",
			 ver->hw_platform, ver->hw_variant);
		if (request_firmware(&fw, fwname, &hdev->dev) < 0) {
			BT_ERR("%s failed to open default Intel fw file: %s",
			       hdev->name, fwname);
			return NULL;
		}
	}

	BT_INFO("%s: Intel Bluetooth firmware file: %s", hdev->name, fwname);

	return fw;
}

static int btusb_setup_intel_patching(struct hci_dev *hdev,
				      const struct firmware *fw,
				      const u8 **fw_ptr, int *disable_patch)
{
	struct sk_buff *skb;
	struct hci_command_hdr *cmd;
	const u8 *cmd_param;
	struct hci_event_hdr *evt = NULL;
	const u8 *evt_param = NULL;
	int remain = fw->size - (*fw_ptr - fw->data);

	/* The first byte indicates the types of the patch command or event.
	 * 0x01 means HCI command and 0x02 is HCI event. If the first bytes
	 * in the current firmware buffer doesn't start with 0x01 or
	 * the size of remain buffer is smaller than HCI command header,
	 * the firmware file is corrupted and it should stop the patching
	 * process.
	 */
	if (remain > HCI_COMMAND_HDR_SIZE && *fw_ptr[0] != 0x01) {
		BT_ERR("%s Intel fw corrupted: invalid cmd read", hdev->name);
		return -EINVAL;
	}
	(*fw_ptr)++;
	remain--;

	cmd = (struct hci_command_hdr *)(*fw_ptr);
	*fw_ptr += sizeof(*cmd);
	remain -= sizeof(*cmd);

	/* Ensure that the remain firmware data is long enough than the length
	 * of command parameter. If not, the firmware file is corrupted.
	 */
	if (remain < cmd->plen) {
		BT_ERR("%s Intel fw corrupted: invalid cmd len", hdev->name);
		return -EFAULT;
	}

	/* If there is a command that loads a patch in the firmware
	 * file, then enable the patch upon success, otherwise just
	 * disable the manufacturer mode, for example patch activation
	 * is not required when the default firmware patch file is used
	 * because there are no patch data to load.
	 */
	if (*disable_patch && le16_to_cpu(cmd->opcode) == 0xfc8e)
		*disable_patch = 0;

	cmd_param = *fw_ptr;
	*fw_ptr += cmd->plen;
	remain -= cmd->plen;

	/* This reads the expected events when the above command is sent to the
	 * device. Some vendor commands expects more than one events, for
	 * example command status event followed by vendor specific event.
	 * For this case, it only keeps the last expected event. so the command
	 * can be sent with __hci_cmd_sync_ev() which returns the sk_buff of
	 * last expected event.
	 */
	while (remain > HCI_EVENT_HDR_SIZE && *fw_ptr[0] == 0x02) {
		(*fw_ptr)++;
		remain--;

		evt = (struct hci_event_hdr *)(*fw_ptr);
		*fw_ptr += sizeof(*evt);
		remain -= sizeof(*evt);

		if (remain < evt->plen) {
			BT_ERR("%s Intel fw corrupted: invalid evt len",
			       hdev->name);
			return -EFAULT;
		}

		evt_param = *fw_ptr;
		*fw_ptr += evt->plen;
		remain -= evt->plen;
	}

	/* Every HCI commands in the firmware file has its correspond event.
	 * If event is not found or remain is smaller than zero, the firmware
	 * file is corrupted.
	 */
	if (!evt || !evt_param || remain < 0) {
		BT_ERR("%s Intel fw corrupted: invalid evt read", hdev->name);
		return -EFAULT;
	}

	skb = __hci_cmd_sync_ev(hdev, le16_to_cpu(cmd->opcode), cmd->plen,
				cmd_param, evt->evt, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s sending Intel patch command (0x%4.4x) failed (%ld)",
		       hdev->name, cmd->opcode, PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	/* It ensures that the returned event matches the event data read from
	 * the firmware file. At fist, it checks the length and then
	 * the contents of the event.
	 */
	if (skb->len != evt->plen) {
		BT_ERR("%s mismatch event length (opcode 0x%4.4x)", hdev->name,
		       le16_to_cpu(cmd->opcode));
		kfree_skb(skb);
		return -EFAULT;
	}

	if (memcmp(skb->data, evt_param, evt->plen)) {
		BT_ERR("%s mismatch event parameter (opcode 0x%4.4x)",
		       hdev->name, le16_to_cpu(cmd->opcode));
		kfree_skb(skb);
		return -EFAULT;
	}
	kfree_skb(skb);

	return 0;
}

static int btusb_setup_intel(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	const struct firmware *fw;
	const u8 *fw_ptr;
	int disable_patch;
	struct intel_version *ver;

	const u8 mfg_enable[] = { 0x01, 0x00 };
	const u8 mfg_disable[] = { 0x00, 0x00 };
	const u8 mfg_reset_deactivate[] = { 0x00, 0x01 };
	const u8 mfg_reset_activate[] = { 0x00, 0x02 };

	BT_DBG("%s", hdev->name);

	/* The controller has a bug with the first HCI command sent to it
	 * returning number of completed commands as zero. This would stall the
	 * command processing in the Bluetooth core.
	 *
	 * As a workaround, send HCI Reset command first which will reset the
	 * number of completed commands and allow normal command processing
	 * from now on.
	 */
	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s sending initial HCI reset command failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	/* Read Intel specific controller version first to allow selection of
	 * which firmware file to load.
	 *
	 * The returned information are hardware variant and revision plus
	 * firmware variant, revision and build number.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc05, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s reading Intel fw version command failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(*ver)) {
		BT_ERR("%s Intel version event length mismatch", hdev->name);
		kfree_skb(skb);
		return -EIO;
	}

	ver = (struct intel_version *)skb->data;

	BT_INFO("%s: read Intel version: %02x%02x%02x%02x%02x%02x%02x%02x%02x",
		hdev->name, ver->hw_platform, ver->hw_variant,
		ver->hw_revision, ver->fw_variant,  ver->fw_revision,
		ver->fw_build_num, ver->fw_build_ww, ver->fw_build_yy,
		ver->fw_patch_num);

	/* fw_patch_num indicates the version of patch the device currently
	 * have. If there is no patch data in the device, it is always 0x00.
	 * So, if it is other than 0x00, no need to patch the device again.
	 */
	if (ver->fw_patch_num) {
		BT_INFO("%s: Intel device is already patched. patch num: %02x",
			hdev->name, ver->fw_patch_num);
		kfree_skb(skb);
		goto complete;
	}

	/* Opens the firmware patch file based on the firmware version read
	 * from the controller. If it fails to open the matching firmware
	 * patch file, it tries to open the default firmware patch file.
	 * If no patch file is found, allow the device to operate without
	 * a patch.
	 */
	fw = btusb_setup_intel_get_fw(hdev, ver);
	if (!fw) {
		kfree_skb(skb);
		goto complete;
	}
	fw_ptr = fw->data;

	kfree_skb(skb);

	/* This Intel specific command enables the manufacturer mode of the
	 * controller.
	 *
	 * Only while this mode is enabled, the driver can download the
	 * firmware patch data and configuration parameters.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc11, 2, mfg_enable, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s entering Intel manufacturer mode failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		release_firmware(fw);
		return PTR_ERR(skb);
	}

	kfree_skb(skb);

	disable_patch = 1;

	/* The firmware data file consists of list of Intel specific HCI
	 * commands and its expected events. The first byte indicates the
	 * type of the message, either HCI command or HCI event.
	 *
	 * It reads the command and its expected event from the firmware file,
	 * and send to the controller. Once __hci_cmd_sync_ev() returns,
	 * the returned event is compared with the event read from the firmware
	 * file and it will continue until all the messages are downloaded to
	 * the controller.
	 *
	 * Once the firmware patching is completed successfully,
	 * the manufacturer mode is disabled with reset and activating the
	 * downloaded patch.
	 *
	 * If the firmware patching fails, the manufacturer mode is
	 * disabled with reset and deactivating the patch.
	 *
	 * If the default patch file is used, no reset is done when disabling
	 * the manufacturer.
	 */
	while (fw->size > fw_ptr - fw->data) {
		int ret;

		ret = btusb_setup_intel_patching(hdev, fw, &fw_ptr,
						 &disable_patch);
		if (ret < 0)
			goto exit_mfg_deactivate;
	}

	release_firmware(fw);

	if (disable_patch)
		goto exit_mfg_disable;

	/* Patching completed successfully and disable the manufacturer mode
	 * with reset and activate the downloaded firmware patches.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc11, sizeof(mfg_reset_activate),
			     mfg_reset_activate, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s exiting Intel manufacturer mode failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	BT_INFO("%s: Intel Bluetooth firmware patch completed and activated",
		hdev->name);

	goto complete;

exit_mfg_disable:
	/* Disable the manufacturer mode without reset */
	skb = __hci_cmd_sync(hdev, 0xfc11, sizeof(mfg_disable), mfg_disable,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s exiting Intel manufacturer mode failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	BT_INFO("%s: Intel Bluetooth firmware patch completed", hdev->name);

	goto complete;

exit_mfg_deactivate:
	release_firmware(fw);

	/* Patching failed. Disable the manufacturer mode with reset and
	 * deactivate the downloaded firmware patches.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc11, sizeof(mfg_reset_deactivate),
			     mfg_reset_deactivate, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s exiting Intel manufacturer mode failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	BT_INFO("%s: Intel Bluetooth firmware patch completed and deactivated",
		hdev->name);

complete:
	/* Set the event mask for Intel specific vendor events. This enables
	 * a few extra events that are useful during general operation.
	 */
	btintel_set_event_mask_mfg(hdev, false);

	btintel_check_bdaddr(hdev);
	return 0;
}

static int inject_cmd_complete(struct hci_dev *hdev, __u16 opcode)
{
	struct sk_buff *skb;
	struct hci_event_hdr *hdr;
	struct hci_ev_cmd_complete *evt;

	skb = bt_skb_alloc(sizeof(*hdr) + sizeof(*evt) + 1, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = (struct hci_event_hdr *)skb_put(skb, sizeof(*hdr));
	hdr->evt = HCI_EV_CMD_COMPLETE;
	hdr->plen = sizeof(*evt) + 1;

	evt = (struct hci_ev_cmd_complete *)skb_put(skb, sizeof(*evt));
	evt->ncmd = 0x01;
	evt->opcode = cpu_to_le16(opcode);

	*skb_put(skb, 1) = 0x00;

	bt_cb(skb)->pkt_type = HCI_EVENT_PKT;

	return hci_recv_frame(hdev, skb);
}

static int btusb_recv_bulk_intel(struct btusb_data *data, void *buffer,
				 int count)
{
	/* When the device is in bootloader mode, then it can send
	 * events via the bulk endpoint. These events are treated the
	 * same way as the ones received from the interrupt endpoint.
	 */
	if (test_bit(BTUSB_BOOTLOADER, &data->flags))
		return btusb_recv_intr(data, buffer, count);

	return btusb_recv_bulk(data, buffer, count);
}

static void btusb_intel_bootup(struct btusb_data *data, const void *ptr,
			       unsigned int len)
{
	const struct intel_bootup *evt = ptr;

	if (len != sizeof(*evt))
		return;

	if (test_and_clear_bit(BTUSB_BOOTING, &data->flags)) {
		smp_mb__after_atomic();
		wake_up_bit(&data->flags, BTUSB_BOOTING);
	}
}

static void btusb_intel_secure_send_result(struct btusb_data *data,
					   const void *ptr, unsigned int len)
{
	const struct intel_secure_send_result *evt = ptr;

	if (len != sizeof(*evt))
		return;

	if (evt->result)
		set_bit(BTUSB_FIRMWARE_FAILED, &data->flags);

	if (test_and_clear_bit(BTUSB_DOWNLOADING, &data->flags) &&
	    test_bit(BTUSB_FIRMWARE_LOADED, &data->flags)) {
		smp_mb__after_atomic();
		wake_up_bit(&data->flags, BTUSB_DOWNLOADING);
	}
}

static int btusb_recv_event_intel(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);

	if (test_bit(BTUSB_BOOTLOADER, &data->flags)) {
		struct hci_event_hdr *hdr = (void *)skb->data;

		if (skb->len > HCI_EVENT_HDR_SIZE && hdr->evt == 0xff &&
		    hdr->plen > 0) {
			const void *ptr = skb->data + HCI_EVENT_HDR_SIZE + 1;
			unsigned int len = skb->len - HCI_EVENT_HDR_SIZE - 1;

			switch (skb->data[2]) {
			case 0x02:
				/* When switching to the operational firmware
				 * the device sends a vendor specific event
				 * indicating that the bootup completed.
				 */
				btusb_intel_bootup(data, ptr, len);
				break;
			case 0x06:
				/* When the firmware loading completes the
				 * device sends out a vendor specific event
				 * indicating the result of the firmware
				 * loading.
				 */
				btusb_intel_secure_send_result(data, ptr, len);
				break;
			}
		}
	}

	return hci_recv_frame(hdev, skb);
}

static int btusb_send_frame_intel(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;

	BT_DBG("%s", hdev->name);

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		if (test_bit(BTUSB_BOOTLOADER, &data->flags)) {
			struct hci_command_hdr *cmd = (void *)skb->data;
			__u16 opcode = le16_to_cpu(cmd->opcode);

			/* When in bootloader mode and the command 0xfc09
			 * is received, it needs to be send down the
			 * bulk endpoint. So allocate a bulk URB instead.
			 */
			if (opcode == 0xfc09)
				urb = alloc_bulk_urb(hdev, skb);
			else
				urb = alloc_ctrl_urb(hdev, skb);

			/* When the 0xfc01 command is issued to boot into
			 * the operational firmware, it will actually not
			 * send a command complete event. To keep the flow
			 * control working inject that event here.
			 */
			if (opcode == 0xfc01)
				inject_cmd_complete(hdev, opcode);
		} else {
			urb = alloc_ctrl_urb(hdev, skb);
		}
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.cmd_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_ACLDATA_PKT:
		urb = alloc_bulk_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.acl_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_SCODATA_PKT:
		if (hci_conn_num(hdev, SCO_LINK) < 1)
			return -ENODEV;

		urb = alloc_isoc_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.sco_tx++;
		return submit_tx_urb(hdev, urb);
	}

	return -EILSEQ;
}

static int btusb_setup_intel_new(struct hci_dev *hdev)
{
	static const u8 reset_param[] = { 0x00, 0x01, 0x00, 0x01,
					  0x00, 0x08, 0x04, 0x00 };
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct sk_buff *skb;
	struct intel_version *ver;
	struct intel_boot_params *params;
	const struct firmware *fw;
	const u8 *fw_ptr;
	u32 frag_len;
	char fwname[64];
	ktime_t calltime, delta, rettime;
	unsigned long long duration;
	int err;

	BT_DBG("%s", hdev->name);

	calltime = ktime_get();

	/* Read the Intel version information to determine if the device
	 * is in bootloader mode or if it already has operational firmware
	 * loaded.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc05, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: Reading Intel version information failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(*ver)) {
		BT_ERR("%s: Intel version event size mismatch", hdev->name);
		kfree_skb(skb);
		return -EILSEQ;
	}

	ver = (struct intel_version *)skb->data;

	/* The hardware platform number has a fixed value of 0x37 and
	 * for now only accept this single value.
	 */
	if (ver->hw_platform != 0x37) {
		BT_ERR("%s: Unsupported Intel hardware platform (%u)",
		       hdev->name, ver->hw_platform);
		kfree_skb(skb);
		return -EINVAL;
	}

	/* At the moment only the hardware variant iBT 3.0 (LnP/SfP) is
	 * supported by this firmware loading method. This check has been
	 * put in place to ensure correct forward compatibility options
	 * when newer hardware variants come along.
	 */
	if (ver->hw_variant != 0x0b) {
		BT_ERR("%s: Unsupported Intel hardware variant (%u)",
		       hdev->name, ver->hw_variant);
		kfree_skb(skb);
		return -EINVAL;
	}

	btintel_version_info(hdev, ver);

	/* The firmware variant determines if the device is in bootloader
	 * mode or is running operational firmware. The value 0x06 identifies
	 * the bootloader and the value 0x23 identifies the operational
	 * firmware.
	 *
	 * When the operational firmware is already present, then only
	 * the check for valid Bluetooth device address is needed. This
	 * determines if the device will be added as configured or
	 * unconfigured controller.
	 *
	 * It is not possible to use the Secure Boot Parameters in this
	 * case since that command is only available in bootloader mode.
	 */
	if (ver->fw_variant == 0x23) {
		kfree_skb(skb);
		clear_bit(BTUSB_BOOTLOADER, &data->flags);
		btintel_check_bdaddr(hdev);
		return 0;
	}

	/* If the device is not in bootloader mode, then the only possible
	 * choice is to return an error and abort the device initialization.
	 */
	if (ver->fw_variant != 0x06) {
		BT_ERR("%s: Unsupported Intel firmware variant (%u)",
		       hdev->name, ver->fw_variant);
		kfree_skb(skb);
		return -ENODEV;
	}

	kfree_skb(skb);

	/* Read the secure boot parameters to identify the operating
	 * details of the bootloader.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc0d, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: Reading Intel boot parameters failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(*params)) {
		BT_ERR("%s: Intel boot parameters size mismatch", hdev->name);
		kfree_skb(skb);
		return -EILSEQ;
	}

	params = (struct intel_boot_params *)skb->data;

	BT_INFO("%s: Device revision is %u", hdev->name,
		le16_to_cpu(params->dev_revid));

	BT_INFO("%s: Secure boot is %s", hdev->name,
		params->secure_boot ? "enabled" : "disabled");

	BT_INFO("%s: OTP lock is %s", hdev->name,
		params->otp_lock ? "enabled" : "disabled");

	BT_INFO("%s: API lock is %s", hdev->name,
		params->api_lock ? "enabled" : "disabled");

	BT_INFO("%s: Debug lock is %s", hdev->name,
		params->debug_lock ? "enabled" : "disabled");

	BT_INFO("%s: Minimum firmware build %u week %u %u", hdev->name,
		params->min_fw_build_nn, params->min_fw_build_cw,
		2000 + params->min_fw_build_yy);

	/* It is required that every single firmware fragment is acknowledged
	 * with a command complete event. If the boot parameters indicate
	 * that this bootloader does not send them, then abort the setup.
	 */
	if (params->limited_cce != 0x00) {
		BT_ERR("%s: Unsupported Intel firmware loading method (%u)",
		       hdev->name, params->limited_cce);
		kfree_skb(skb);
		return -EINVAL;
	}

	/* If the OTP has no valid Bluetooth device address, then there will
	 * also be no valid address for the operational firmware.
	 */
	if (!bacmp(&params->otp_bdaddr, BDADDR_ANY)) {
		BT_INFO("%s: No device address configured", hdev->name);
		set_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks);
	}

	/* With this Intel bootloader only the hardware variant and device
	 * revision information are used to select the right firmware.
	 *
	 * Currently this bootloader support is limited to hardware variant
	 * iBT 3.0 (LnP/SfP) which is identified by the value 11 (0x0b).
	 */
	snprintf(fwname, sizeof(fwname), "intel/ibt-11-%u.sfi",
		 le16_to_cpu(params->dev_revid));

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err < 0) {
		BT_ERR("%s: Failed to load Intel firmware file (%d)",
		       hdev->name, err);
		kfree_skb(skb);
		return err;
	}

	BT_INFO("%s: Found device firmware: %s", hdev->name, fwname);

	/* Save the DDC file name for later use to apply once the firmware
	 * downloading is done.
	 */
	snprintf(fwname, sizeof(fwname), "intel/ibt-11-%u.ddc",
		 le16_to_cpu(params->dev_revid));

	kfree_skb(skb);

	if (fw->size < 644) {
		BT_ERR("%s: Invalid size of firmware file (%zu)",
		       hdev->name, fw->size);
		err = -EBADF;
		goto done;
	}

	set_bit(BTUSB_DOWNLOADING, &data->flags);

	/* Start the firmware download transaction with the Init fragment
	 * represented by the 128 bytes of CSS header.
	 */
	err = btintel_secure_send(hdev, 0x00, 128, fw->data);
	if (err < 0) {
		BT_ERR("%s: Failed to send firmware header (%d)",
		       hdev->name, err);
		goto done;
	}

	/* Send the 256 bytes of public key information from the firmware
	 * as the PKey fragment.
	 */
	err = btintel_secure_send(hdev, 0x03, 256, fw->data + 128);
	if (err < 0) {
		BT_ERR("%s: Failed to send firmware public key (%d)",
		       hdev->name, err);
		goto done;
	}

	/* Send the 256 bytes of signature information from the firmware
	 * as the Sign fragment.
	 */
	err = btintel_secure_send(hdev, 0x02, 256, fw->data + 388);
	if (err < 0) {
		BT_ERR("%s: Failed to send firmware signature (%d)",
		       hdev->name, err);
		goto done;
	}

	fw_ptr = fw->data + 644;
	frag_len = 0;

	while (fw_ptr - fw->data < fw->size) {
		struct hci_command_hdr *cmd = (void *)(fw_ptr + frag_len);

		frag_len += sizeof(*cmd) + cmd->plen;

		/* The parameter length of the secure send command requires
		 * a 4 byte alignment. It happens so that the firmware file
		 * contains proper Intel_NOP commands to align the fragments
		 * as needed.
		 *
		 * Send set of commands with 4 byte alignment from the
		 * firmware data buffer as a single Data fragement.
		 */
		if (!(frag_len % 4)) {
			err = btintel_secure_send(hdev, 0x01, frag_len, fw_ptr);
			if (err < 0) {
				BT_ERR("%s: Failed to send firmware data (%d)",
				       hdev->name, err);
				goto done;
			}

			fw_ptr += frag_len;
			frag_len = 0;
		}
	}

	set_bit(BTUSB_FIRMWARE_LOADED, &data->flags);

	BT_INFO("%s: Waiting for firmware download to complete", hdev->name);

	/* Before switching the device into operational mode and with that
	 * booting the loaded firmware, wait for the bootloader notification
	 * that all fragments have been successfully received.
	 *
	 * When the event processing receives the notification, then the
	 * BTUSB_DOWNLOADING flag will be cleared.
	 *
	 * The firmware loading should not take longer than 5 seconds
	 * and thus just timeout if that happens and fail the setup
	 * of this device.
	 */
	err = wait_on_bit_timeout(&data->flags, BTUSB_DOWNLOADING,
				  TASK_INTERRUPTIBLE,
				  msecs_to_jiffies(5000));
	if (err == 1) {
		BT_ERR("%s: Firmware loading interrupted", hdev->name);
		err = -EINTR;
		goto done;
	}

	if (err) {
		BT_ERR("%s: Firmware loading timeout", hdev->name);
		err = -ETIMEDOUT;
		goto done;
	}

	if (test_bit(BTUSB_FIRMWARE_FAILED, &data->flags)) {
		BT_ERR("%s: Firmware loading failed", hdev->name);
		err = -ENOEXEC;
		goto done;
	}

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;

	BT_INFO("%s: Firmware loaded in %llu usecs", hdev->name, duration);

done:
	release_firmware(fw);

	if (err < 0)
		return err;

	calltime = ktime_get();

	set_bit(BTUSB_BOOTING, &data->flags);

	skb = __hci_cmd_sync(hdev, 0xfc01, sizeof(reset_param), reset_param,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	kfree_skb(skb);

	/* The bootloader will not indicate when the device is ready. This
	 * is done by the operational firmware sending bootup notification.
	 *
	 * Booting into operational firmware should not take longer than
	 * 1 second. However if that happens, then just fail the setup
	 * since something went wrong.
	 */
	BT_INFO("%s: Waiting for device to boot", hdev->name);

	err = wait_on_bit_timeout(&data->flags, BTUSB_BOOTING,
				  TASK_INTERRUPTIBLE,
				  msecs_to_jiffies(1000));

	if (err == 1) {
		BT_ERR("%s: Device boot interrupted", hdev->name);
		return -EINTR;
	}

	if (err) {
		BT_ERR("%s: Device boot timeout", hdev->name);
		return -ETIMEDOUT;
	}

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;

	BT_INFO("%s: Device booted in %llu usecs", hdev->name, duration);

	clear_bit(BTUSB_BOOTLOADER, &data->flags);

	/* Once the device is running in operational mode, it needs to apply
	 * the device configuration (DDC) parameters.
	 *
	 * The device can work without DDC parameters, so even if it fails
	 * to load the file, no need to fail the setup.
	 */
	btintel_load_ddc_config(hdev, fwname);

	/* Set the event mask for Intel specific vendor events. This enables
	 * a few extra events that are useful during general operation. It
	 * does not enable any debugging related events.
	 *
	 * The device will function correctly without these events enabled
	 * and thus no need to fail the setup.
	 */
	btintel_set_event_mask(hdev, false);

	return 0;
}

static int btusb_shutdown_intel(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	long ret;

	/* Some platforms have an issue with BT LED when the interface is
	 * down or BT radio is turned off, which takes 5 seconds to BT LED
	 * goes off. This command turns off the BT LED immediately.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc3f, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		BT_ERR("%s: turning off Intel device LED failed (%ld)",
		       hdev->name, ret);
		return ret;
	}
	kfree_skb(skb);

	return 0;
}

static int btusb_set_bdaddr_marvell(struct hci_dev *hdev,
				    const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	u8 buf[8];
	long ret;

	buf[0] = 0xfe;
	buf[1] = sizeof(bdaddr_t);
	memcpy(buf + 2, bdaddr, sizeof(bdaddr_t));

	skb = __hci_cmd_sync(hdev, 0xfc22, sizeof(buf), buf, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		BT_ERR("%s: changing Marvell device address failed (%ld)",
		       hdev->name, ret);
		return ret;
	}
	kfree_skb(skb);

	return 0;
}

static int btusb_set_bdaddr_ath3012(struct hci_dev *hdev,
				    const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	u8 buf[10];
	long ret;

	buf[0] = 0x01;
	buf[1] = 0x01;
	buf[2] = 0x00;
	buf[3] = sizeof(bdaddr_t);
	memcpy(buf + 4, bdaddr, sizeof(bdaddr_t));

	skb = __hci_cmd_sync(hdev, 0xfc0b, sizeof(buf), buf, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		BT_ERR("%s: Change address command failed (%ld)",
		       hdev->name, ret);
		return ret;
	}
	kfree_skb(skb);

	return 0;
}

#define QCA_DFU_PACKET_LEN	4096

#define QCA_GET_TARGET_VERSION	0x09
#define QCA_CHECK_STATUS	0x05
#define QCA_DFU_DOWNLOAD	0x01

#define QCA_SYSCFG_UPDATED	0x40
#define QCA_PATCH_UPDATED	0x80
#define QCA_DFU_TIMEOUT		3000

struct qca_version {
	__le32	rom_version;
	__le32	patch_version;
	__le32	ram_version;
	__le32	ref_clock;
	__u8	reserved[4];
} __packed;

struct qca_rampatch_version {
	__le16	rom_version;
	__le16	patch_version;
} __packed;

struct qca_device_info {
	u32	rom_version;
	u8	rampatch_hdr;	/* length of header in rampatch */
	u8	nvm_hdr;	/* length of header in NVM */
	u8	ver_offset;	/* offset of version structure in rampatch */
};

static const struct qca_device_info qca_devices_table[] = {
	{ 0x00000100, 20, 4, 10 }, /* Rome 1.0 */
	{ 0x00000101, 20, 4, 10 }, /* Rome 1.1 */
	{ 0x00000200, 28, 4, 18 }, /* Rome 2.0 */
	{ 0x00000201, 28, 4, 18 }, /* Rome 2.1 */
	{ 0x00000300, 28, 4, 18 }, /* Rome 3.0 */
	{ 0x00000302, 28, 4, 18 }, /* Rome 3.2 */
};

static int btusb_qca_send_vendor_req(struct hci_dev *hdev, u8 request,
				     void *data, u16 size)
{
	struct btusb_data *btdata = hci_get_drvdata(hdev);
	struct usb_device *udev = btdata->udev;
	int pipe, err;
	u8 *buf;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Found some of USB hosts have IOT issues with ours so that we should
	 * not wait until HCI layer is ready.
	 */
	pipe = usb_rcvctrlpipe(udev, 0);
	err = usb_control_msg(udev, pipe, request, USB_TYPE_VENDOR | USB_DIR_IN,
			      0, 0, buf, size, USB_CTRL_SET_TIMEOUT);
	if (err < 0) {
		BT_ERR("%s: Failed to access otp area (%d)", hdev->name, err);
		goto done;
	}

	memcpy(data, buf, size);

done:
	kfree(buf);

	return err;
}

static int btusb_setup_qca_download_fw(struct hci_dev *hdev,
				       const struct firmware *firmware,
				       size_t hdr_size)
{
	struct btusb_data *btdata = hci_get_drvdata(hdev);
	struct usb_device *udev = btdata->udev;
	size_t count, size, sent = 0;
	int pipe, len, err;
	u8 *buf;

	buf = kmalloc(QCA_DFU_PACKET_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	count = firmware->size;

	size = min_t(size_t, count, hdr_size);
	memcpy(buf, firmware->data, size);

	/* USB patches should go down to controller through USB path
	 * because binary format fits to go down through USB channel.
	 * USB control path is for patching headers and USB bulk is for
	 * patch body.
	 */
	pipe = usb_sndctrlpipe(udev, 0);
	err = usb_control_msg(udev, pipe, QCA_DFU_DOWNLOAD, USB_TYPE_VENDOR,
			      0, 0, buf, size, USB_CTRL_SET_TIMEOUT);
	if (err < 0) {
		BT_ERR("%s: Failed to send headers (%d)", hdev->name, err);
		goto done;
	}

	sent += size;
	count -= size;

	while (count) {
		size = min_t(size_t, count, QCA_DFU_PACKET_LEN);

		memcpy(buf, firmware->data + sent, size);

		pipe = usb_sndbulkpipe(udev, 0x02);
		err = usb_bulk_msg(udev, pipe, buf, size, &len,
				   QCA_DFU_TIMEOUT);
		if (err < 0) {
			BT_ERR("%s: Failed to send body at %zd of %zd (%d)",
			       hdev->name, sent, firmware->size, err);
			break;
		}

		if (size != len) {
			BT_ERR("%s: Failed to get bulk buffer", hdev->name);
			err = -EILSEQ;
			break;
		}

		sent  += size;
		count -= size;
	}

done:
	kfree(buf);
	return err;
}

static int btusb_setup_qca_load_rampatch(struct hci_dev *hdev,
					 struct qca_version *ver,
					 const struct qca_device_info *info)
{
	struct qca_rampatch_version *rver;
	const struct firmware *fw;
	u32 ver_rom, ver_patch;
	u16 rver_rom, rver_patch;
	char fwname[64];
	int err;

	ver_rom = le32_to_cpu(ver->rom_version);
	ver_patch = le32_to_cpu(ver->patch_version);

	snprintf(fwname, sizeof(fwname), "qca/rampatch_usb_%08x.bin", ver_rom);

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err) {
		BT_ERR("%s: failed to request rampatch file: %s (%d)",
		       hdev->name, fwname, err);
		return err;
	}

	BT_INFO("%s: using rampatch file: %s", hdev->name, fwname);

	rver = (struct qca_rampatch_version *)(fw->data + info->ver_offset);
	rver_rom = le16_to_cpu(rver->rom_version);
	rver_patch = le16_to_cpu(rver->patch_version);

	BT_INFO("%s: QCA: patch rome 0x%x build 0x%x, firmware rome 0x%x "
		"build 0x%x", hdev->name, rver_rom, rver_patch, ver_rom,
		ver_patch);

	if (rver_rom != ver_rom || rver_patch <= ver_patch) {
		BT_ERR("%s: rampatch file version did not match with firmware",
		       hdev->name);
		err = -EINVAL;
		goto done;
	}

	err = btusb_setup_qca_download_fw(hdev, fw, info->rampatch_hdr);

done:
	release_firmware(fw);

	return err;
}

static int btusb_setup_qca_load_nvm(struct hci_dev *hdev,
				    struct qca_version *ver,
				    const struct qca_device_info *info)
{
	const struct firmware *fw;
	char fwname[64];
	int err;

	snprintf(fwname, sizeof(fwname), "qca/nvm_usb_%08x.bin",
		 le32_to_cpu(ver->rom_version));

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err) {
		BT_ERR("%s: failed to request NVM file: %s (%d)",
		       hdev->name, fwname, err);
		return err;
	}

	BT_INFO("%s: using NVM file: %s", hdev->name, fwname);

	err = btusb_setup_qca_download_fw(hdev, fw, info->nvm_hdr);

	release_firmware(fw);

	return err;
}

static int btusb_setup_qca(struct hci_dev *hdev)
{
	const struct qca_device_info *info = NULL;
	struct qca_version ver;
	u32 ver_rom;
	u8 status;
	int i, err;

	err = btusb_qca_send_vendor_req(hdev, QCA_GET_TARGET_VERSION, &ver,
					sizeof(ver));
	if (err < 0)
		return err;

	ver_rom = le32_to_cpu(ver.rom_version);
	for (i = 0; i < ARRAY_SIZE(qca_devices_table); i++) {
		if (ver_rom == qca_devices_table[i].rom_version)
			info = &qca_devices_table[i];
	}
	if (!info) {
		BT_ERR("%s: don't support firmware rome 0x%x", hdev->name,
		       ver_rom);
		return -ENODEV;
	}

	err = btusb_qca_send_vendor_req(hdev, QCA_CHECK_STATUS, &status,
					sizeof(status));
	if (err < 0)
		return err;

	if (!(status & QCA_PATCH_UPDATED)) {
		err = btusb_setup_qca_load_rampatch(hdev, &ver, info);
		if (err < 0)
			return err;
	}

	if (!(status & QCA_SYSCFG_UPDATED)) {
		err = btusb_setup_qca_load_nvm(hdev, &ver, info);
		if (err < 0)
			return err;
	}

	return 0;
}

#ifdef CONFIG_BT_HCIBTUSB_BCM
static inline int __set_diag_interface(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_interface *intf = data->diag;
	int i;

	if (!data->diag)
		return -ENODEV;

	data->diag_tx_ep = NULL;
	data->diag_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep_desc;

		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->diag_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			data->diag_tx_ep = ep_desc;
			continue;
		}

		if (!data->diag_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			data->diag_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->diag_tx_ep || !data->diag_rx_ep) {
		BT_ERR("%s invalid diagnostic descriptors", hdev->name);
		return -ENODEV;
	}

	return 0;
}

static struct urb *alloc_diag_urb(struct hci_dev *hdev, bool enable)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct sk_buff *skb;
	struct urb *urb;
	unsigned int pipe;

	if (!data->diag_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	skb = bt_skb_alloc(2, GFP_KERNEL);
	if (!skb) {
		usb_free_urb(urb);
		return ERR_PTR(-ENOMEM);
	}

	*skb_put(skb, 1) = 0xf0;
	*skb_put(skb, 1) = enable;

	pipe = usb_sndbulkpipe(data->udev, data->diag_tx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe,
			  skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static int btusb_bcm_set_diag(struct hci_dev *hdev, bool enable)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;

	if (!data->diag)
		return -ENODEV;

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -ENETDOWN;

	urb = alloc_diag_urb(hdev, enable);
	if (IS_ERR(urb))
		return PTR_ERR(urb);

	return submit_or_queue_tx_urb(hdev, urb);
}
#endif

static int btusb_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct btusb_data *data;
	struct hci_dev *hdev;
	unsigned ifnum_base;
	int i, err;

	BT_DBG("intf %p id %p", intf, id);

	/* interface numbers are hardcoded in the spec */
	if (intf->cur_altsetting->desc.bInterfaceNumber != 0) {
		if (!(id->driver_info & BTUSB_IFNUM_2))
			return -ENODEV;
		if (intf->cur_altsetting->desc.bInterfaceNumber != 2)
			return -ENODEV;
	}

	ifnum_base = intf->cur_altsetting->desc.bInterfaceNumber;

	if (!id->driver_info) {
		const struct usb_device_id *match;

		match = usb_match_id(intf, blacklist_table);
		if (match)
			id = match;
	}

	if (id->driver_info == BTUSB_IGNORE)
		return -ENODEV;

	if (id->driver_info & BTUSB_ATH3012) {
		struct usb_device *udev = interface_to_usbdev(intf);

		/* Old firmware would otherwise let ath3k driver load
		 * patch and sysconfig files */
		if (le16_to_cpu(udev->descriptor.bcdDevice) <= 0x0001)
			return -ENODEV;
	}

	data = devm_kzalloc(&intf->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->intr_ep && usb_endpoint_is_int_in(ep_desc)) {
			data->intr_ep = ep_desc;
			continue;
		}

		if (!data->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			data->bulk_tx_ep = ep_desc;
			continue;
		}

		if (!data->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			data->bulk_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->intr_ep || !data->bulk_tx_ep || !data->bulk_rx_ep)
		return -ENODEV;

	if (id->driver_info & BTUSB_AMP) {
		data->cmdreq_type = USB_TYPE_CLASS | 0x01;
		data->cmdreq = 0x2b;
	} else {
		data->cmdreq_type = USB_TYPE_CLASS;
		data->cmdreq = 0x00;
	}

	data->udev = interface_to_usbdev(intf);
	data->intf = intf;

	INIT_WORK(&data->work, btusb_work);
	INIT_WORK(&data->waker, btusb_waker);
	init_usb_anchor(&data->deferred);
	init_usb_anchor(&data->tx_anchor);
	spin_lock_init(&data->txlock);

	init_usb_anchor(&data->intr_anchor);
	init_usb_anchor(&data->bulk_anchor);
	init_usb_anchor(&data->isoc_anchor);
	init_usb_anchor(&data->diag_anchor);
	spin_lock_init(&data->rxlock);

	if (id->driver_info & BTUSB_INTEL_NEW) {
		data->recv_event = btusb_recv_event_intel;
		data->recv_bulk = btusb_recv_bulk_intel;
		set_bit(BTUSB_BOOTLOADER, &data->flags);
	} else {
		data->recv_event = hci_recv_frame;
		data->recv_bulk = btusb_recv_bulk;
	}

	hdev = hci_alloc_dev();
	if (!hdev)
		return -ENOMEM;

	hdev->bus = HCI_USB;
	hci_set_drvdata(hdev, data);

	if (id->driver_info & BTUSB_AMP)
		hdev->dev_type = HCI_AMP;
	else
		hdev->dev_type = HCI_BREDR;

	data->hdev = hdev;

	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open   = btusb_open;
	hdev->close  = btusb_close;
	hdev->flush  = btusb_flush;
	hdev->send   = btusb_send_frame;
	hdev->notify = btusb_notify;

	if (id->driver_info & BTUSB_BCM2045)
		set_bit(HCI_QUIRK_BROKEN_STORED_LINK_KEY, &hdev->quirks);

	if (id->driver_info & BTUSB_BCM92035)
		hdev->setup = btusb_setup_bcm92035;

#ifdef CONFIG_BT_HCIBTUSB_BCM
	if (id->driver_info & BTUSB_BCM_PATCHRAM) {
		hdev->manufacturer = 15;
		hdev->setup = btbcm_setup_patchram;
		hdev->set_diag = btusb_bcm_set_diag;
		hdev->set_bdaddr = btbcm_set_bdaddr;

		/* Broadcom LM_DIAG Interface numbers are hardcoded */
		data->diag = usb_ifnum_to_if(data->udev, ifnum_base + 2);
	}

	if (id->driver_info & BTUSB_BCM_APPLE) {
		hdev->manufacturer = 15;
		hdev->setup = btbcm_setup_apple;
		hdev->set_diag = btusb_bcm_set_diag;

		/* Broadcom LM_DIAG Interface numbers are hardcoded */
		data->diag = usb_ifnum_to_if(data->udev, ifnum_base + 2);
	}
#endif

	if (id->driver_info & BTUSB_INTEL) {
		hdev->manufacturer = 2;
		hdev->setup = btusb_setup_intel;
		hdev->shutdown = btusb_shutdown_intel;
		hdev->set_diag = btintel_set_diag_mfg;
		hdev->set_bdaddr = btintel_set_bdaddr;
		set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);
		set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);
		set_bit(HCI_QUIRK_NON_PERSISTENT_DIAG, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_INTEL_NEW) {
		hdev->manufacturer = 2;
		hdev->send = btusb_send_frame_intel;
		hdev->setup = btusb_setup_intel_new;
		hdev->hw_error = btintel_hw_error;
		hdev->set_diag = btintel_set_diag;
		hdev->set_bdaddr = btintel_set_bdaddr;
		set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);
		set_bit(HCI_QUIRK_NON_PERSISTENT_DIAG, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_MARVELL)
		hdev->set_bdaddr = btusb_set_bdaddr_marvell;

	if (id->driver_info & BTUSB_SWAVE) {
		set_bit(HCI_QUIRK_FIXUP_INQUIRY_MODE, &hdev->quirks);
		set_bit(HCI_QUIRK_BROKEN_LOCAL_COMMANDS, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_INTEL_BOOT) {
		hdev->manufacturer = 2;
		set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_ATH3012) {
		hdev->set_bdaddr = btusb_set_bdaddr_ath3012;
		set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);
		set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_QCA_ROME) {
		data->setup_on_usb = btusb_setup_qca;
		hdev->set_bdaddr = btusb_set_bdaddr_ath3012;
	}

#ifdef CONFIG_BT_HCIBTUSB_RTL
	if (id->driver_info & BTUSB_REALTEK) {
		hdev->setup = btrtl_setup_realtek;

		/* Realtek devices lose their updated firmware over suspend,
		 * but the USB hub doesn't notice any status change.
		 * Explicitly request a device reset on resume.
		 */
		set_bit(BTUSB_RESET_RESUME, &data->flags);
	}
#endif

	if (id->driver_info & BTUSB_AMP) {
		/* AMP controllers do not support SCO packets */
		data->isoc = NULL;
	} else {
		/* Interface orders are hardcoded in the specification */
		data->isoc = usb_ifnum_to_if(data->udev, ifnum_base + 1);
	}

	if (!reset)
		set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);

	if (force_scofix || id->driver_info & BTUSB_WRONG_SCO_MTU) {
		if (!disable_scofix)
			set_bit(HCI_QUIRK_FIXUP_BUFFER_SIZE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_BROKEN_ISOC)
		data->isoc = NULL;

	if (id->driver_info & BTUSB_DIGIANSWER) {
		data->cmdreq_type = USB_TYPE_VENDOR;
		set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_CSR) {
		struct usb_device *udev = data->udev;
		u16 bcdDevice = le16_to_cpu(udev->descriptor.bcdDevice);

		/* Old firmware would otherwise execute USB reset */
		if (bcdDevice < 0x117)
			set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);

		/* Fake CSR devices with broken commands */
		if (bcdDevice <= 0x100 || bcdDevice == 0x134)
			hdev->setup = btusb_setup_csr;

		set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_SNIFFER) {
		struct usb_device *udev = data->udev;

		/* New sniffer firmware has crippled HCI interface */
		if (le16_to_cpu(udev->descriptor.bcdDevice) > 0x997)
			set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_INTEL_BOOT) {
		/* A bug in the bootloader causes that interrupt interface is
		 * only enabled after receiving SetInterface(0, AltSetting=0).
		 */
		err = usb_set_interface(data->udev, 0, 0);
		if (err < 0) {
			BT_ERR("failed to set interface 0, alt 0 %d", err);
			hci_free_dev(hdev);
			return err;
		}
	}

	if (data->isoc) {
		err = usb_driver_claim_interface(&btusb_driver,
						 data->isoc, data);
		if (err < 0) {
			hci_free_dev(hdev);
			return err;
		}
	}

#ifdef CONFIG_BT_HCIBTUSB_BCM
	if (data->diag) {
		if (!usb_driver_claim_interface(&btusb_driver,
						data->diag, data))
			__set_diag_interface(hdev);
		else
			data->diag = NULL;
	}
#endif

	err = hci_register_dev(hdev);
	if (err < 0) {
		hci_free_dev(hdev);
		return err;
	}

	usb_set_intfdata(intf, data);

	return 0;
}

static void btusb_disconnect(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev;

	BT_DBG("intf %p", intf);

	if (!data)
		return;

	hdev = data->hdev;
	usb_set_intfdata(data->intf, NULL);

	if (data->isoc)
		usb_set_intfdata(data->isoc, NULL);

	if (data->diag)
		usb_set_intfdata(data->diag, NULL);

	hci_unregister_dev(hdev);

	if (intf == data->intf) {
		if (data->isoc)
			usb_driver_release_interface(&btusb_driver, data->isoc);
		if (data->diag)
			usb_driver_release_interface(&btusb_driver, data->diag);
	} else if (intf == data->isoc) {
		if (data->diag)
			usb_driver_release_interface(&btusb_driver, data->diag);
		usb_driver_release_interface(&btusb_driver, data->intf);
	} else if (intf == data->diag) {
		usb_driver_release_interface(&btusb_driver, data->intf);
		if (data->isoc)
			usb_driver_release_interface(&btusb_driver, data->isoc);
	}

	hci_free_dev(hdev);
}

#ifdef CONFIG_PM
static int btusb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct btusb_data *data = usb_get_intfdata(intf);

	BT_DBG("intf %p", intf);

	if (data->suspend_count++)
		return 0;

	spin_lock_irq(&data->txlock);
	if (!(PMSG_IS_AUTO(message) && data->tx_in_flight)) {
		set_bit(BTUSB_SUSPENDING, &data->flags);
		spin_unlock_irq(&data->txlock);
	} else {
		spin_unlock_irq(&data->txlock);
		data->suspend_count--;
		return -EBUSY;
	}

	cancel_work_sync(&data->work);

	btusb_stop_traffic(data);
	usb_kill_anchored_urbs(&data->tx_anchor);

	/* Optionally request a device reset on resume, but only when
	 * wakeups are disabled. If wakeups are enabled we assume the
	 * device will stay powered up throughout suspend.
	 */
	if (test_bit(BTUSB_RESET_RESUME, &data->flags) &&
	    !device_may_wakeup(&data->udev->dev))
		data->udev->reset_resume = 1;

	return 0;
}

static void play_deferred(struct btusb_data *data)
{
	struct urb *urb;
	int err;

	while ((urb = usb_get_from_anchor(&data->deferred))) {
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0)
			break;

		data->tx_in_flight++;
	}
	usb_scuttle_anchored_urbs(&data->deferred);
}

static int btusb_resume(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev = data->hdev;
	int err = 0;

	BT_DBG("intf %p", intf);

	if (--data->suspend_count)
		return 0;

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (test_bit(BTUSB_INTR_RUNNING, &data->flags)) {
		err = btusb_submit_intr_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_INTR_RUNNING, &data->flags);
			goto failed;
		}
	}

	if (test_bit(BTUSB_BULK_RUNNING, &data->flags)) {
		err = btusb_submit_bulk_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_BULK_RUNNING, &data->flags);
			goto failed;
		}

		btusb_submit_bulk_urb(hdev, GFP_NOIO);
	}

	if (test_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
		if (btusb_submit_isoc_urb(hdev, GFP_NOIO) < 0)
			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		else
			btusb_submit_isoc_urb(hdev, GFP_NOIO);
	}

	spin_lock_irq(&data->txlock);
	play_deferred(data);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);
	schedule_work(&data->work);

	return 0;

failed:
	usb_scuttle_anchored_urbs(&data->deferred);
done:
	spin_lock_irq(&data->txlock);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);

	return err;
}
#endif

static struct usb_driver btusb_driver = {
	.name		= "btusb",
	.probe		= btusb_probe,
	.disconnect	= btusb_disconnect,
#ifdef CONFIG_PM
	.suspend	= btusb_suspend,
	.resume		= btusb_resume,
#endif
	.id_table	= btusb_table,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(btusb_driver);

module_param(disable_scofix, bool, 0644);
MODULE_PARM_DESC(disable_scofix, "Disable fixup of wrong SCO buffer size");

module_param(force_scofix, bool, 0644);
MODULE_PARM_DESC(force_scofix, "Force fixup of wrong SCO buffers size");

module_param(reset, bool, 0644);
MODULE_PARM_DESC(reset, "Send HCI reset command on initialization");

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Generic Bluetooth USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
