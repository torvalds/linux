/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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

#ifndef HTC_USB_H
#define HTC_USB_H

#define MAJOR_VERSION_REQ 1
#define MINOR_VERSION_REQ 3

#define IS_AR7010_DEVICE(_v) (((_v) == AR9280_USB) || ((_v) == AR9287_USB))

#define AR9271_FIRMWARE       0x501000
#define AR9271_FIRMWARE_TEXT  0x903000
#define AR7010_FIRMWARE_TEXT  0x906000

#define FIRMWARE_DOWNLOAD       0x30
#define FIRMWARE_DOWNLOAD_COMP  0x31

#define ATH_USB_RX_STREAM_MODE_TAG 0x4e00
#define ATH_USB_TX_STREAM_MODE_TAG 0x697e

/* FIXME: Verify these numbers (with Windows) */
#define MAX_TX_URB_NUM  8
#define MAX_TX_BUF_NUM  256
#define MAX_TX_BUF_SIZE 32768
#define MAX_TX_AGGR_NUM 20

#define MAX_RX_URB_NUM  8
#define MAX_RX_BUF_SIZE 16384
#define MAX_PKT_NUM_IN_TRANSFER 10

#define MAX_REG_OUT_URB_NUM  1
#define MAX_REG_IN_URB_NUM   64

#define MAX_REG_IN_BUF_SIZE 64

/* USB Endpoint definition */
#define USB_WLAN_TX_PIPE  1
#define USB_WLAN_RX_PIPE  2
#define USB_REG_IN_PIPE   3
#define USB_REG_OUT_PIPE  4

#define HIF_USB_MAX_RXPIPES 2
#define HIF_USB_MAX_TXPIPES 4

struct tx_buf {
	u8 *buf;
	u16 len;
	u16 offset;
	struct urb *urb;
	struct sk_buff_head skb_queue;
	struct hif_device_usb *hif_dev;
	struct list_head list;
};

#define HIF_USB_TX_STOP  BIT(0)
#define HIF_USB_TX_FLUSH BIT(1)

struct hif_usb_tx {
	u8 flags;
	u8 tx_buf_cnt;
	u16 tx_skb_cnt;
	struct sk_buff_head tx_skb_queue;
	struct list_head tx_buf;
	struct list_head tx_pending;
	spinlock_t tx_lock;
};

struct cmd_buf {
	struct sk_buff *skb;
	struct hif_device_usb *hif_dev;
};

#define HIF_USB_START BIT(0)
#define HIF_USB_READY BIT(1)

struct hif_device_usb {
	struct usb_device *udev;
	struct usb_interface *interface;
	const struct usb_device_id *usb_device_id;
	const void *fw_data;
	size_t fw_size;
	struct completion fw_done;
	struct htc_target *htc_handle;
	struct hif_usb_tx tx;
	struct usb_anchor regout_submitted;
	struct usb_anchor rx_submitted;
	struct usb_anchor reg_in_submitted;
	struct usb_anchor mgmt_submitted;
	struct sk_buff *remain_skb;
	const char *fw_name;
	int rx_remain_len;
	int rx_pkt_len;
	int rx_transfer_len;
	int rx_pad_len;
	spinlock_t rx_lock;
	u8 flags; /* HIF_USB_* */
};

int ath9k_hif_usb_init(void);
void ath9k_hif_usb_exit(void);

#endif /* HTC_USB_H */
