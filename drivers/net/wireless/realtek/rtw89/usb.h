/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2025  Realtek Corporation
 */

#ifndef __RTW89_USB_H__
#define __RTW89_USB_H__

#include "txrx.h"

#define RTW89_USB_VENQT			0x05
#define RTW89_USB_VENQT_READ		0xc0
#define RTW89_USB_VENQT_WRITE		0x40

#define RTW89_USB_RECVBUF_SZ		20480
#define RTW89_USB_RXCB_NUM		8
#define RTW89_USB_RX_SKB_NUM		16
#define RTW89_USB_MAX_RXQ_LEN		512
#define RTW89_USB_MOD512_PADDING	4

#define RTW89_MAX_ENDPOINT_NUM		9
#define RTW89_MAX_BULKOUT_NUM		7

struct rtw89_usb_rx_ctrl_block {
	struct rtw89_dev *rtwdev;
	struct urb *rx_urb;
	struct sk_buff *rx_skb;
};

struct rtw89_usb_tx_ctrl_block {
	struct rtw89_dev *rtwdev;
	u8 txch;
	struct sk_buff_head tx_ack_queue;
};

struct rtw89_usb {
	struct rtw89_dev *rtwdev;
	struct usb_device *udev;

	__le32 *vendor_req_buf;

	atomic_t continual_io_error;

	u8 in_pipe;
	u8 out_pipe[RTW89_MAX_BULKOUT_NUM];

	struct workqueue_struct *rxwq;
	struct rtw89_usb_rx_ctrl_block rx_cb[RTW89_USB_RXCB_NUM];
	struct sk_buff_head rx_queue;
	struct sk_buff_head rx_free_queue;
	struct work_struct rx_work;
	struct work_struct rx_urb_work;

	struct sk_buff_head tx_queue[RTW89_TXCH_NUM];
};

static inline struct rtw89_usb *rtw89_usb_priv(struct rtw89_dev *rtwdev)
{
	return (struct rtw89_usb *)rtwdev->priv;
}

int rtw89_usb_probe(struct usb_interface *intf,
		    const struct usb_device_id *id);
void rtw89_usb_disconnect(struct usb_interface *intf);

#endif
