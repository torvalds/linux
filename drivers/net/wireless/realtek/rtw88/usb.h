/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_USB_H_
#define __RTW_USB_H_

#define FW_8192C_START_ADDRESS		0x1000
#define FW_8192C_END_ADDRESS		0x5fff

#define RTW_USB_MAX_RXTX_COUNT		128
#define RTW_USB_VENQT_MAX_BUF_SIZE	254
#define MAX_USBCTRL_VENDORREQ_TIMES	10

#define RTW_USB_CMD_READ		0xc0
#define RTW_USB_CMD_WRITE		0x40
#define RTW_USB_CMD_REQ			0x05

#define RTW_USB_VENQT_CMD_IDX		0x00

#define RTW_USB_TX_SEL_HQ		BIT(0)
#define RTW_USB_TX_SEL_LQ		BIT(1)
#define RTW_USB_TX_SEL_NQ		BIT(2)
#define RTW_USB_TX_SEL_EQ		BIT(3)

#define RTW_USB_BULK_IN_ADDR		0x80
#define RTW_USB_INT_IN_ADDR		0x81

#define RTW_USB_HW_QUEUE_ENTRY		8

#define RTW_USB_PACKET_OFFSET_SZ	8
#define RTW_USB_MAX_XMITBUF_SZ		(1024 * 20)
#define RTW_USB_MAX_RECVBUF_SZ		32768

#define RTW_USB_RECVBUFF_ALIGN_SZ	8

#define RTW_USB_RXAGG_SIZE		6
#define RTW_USB_RXAGG_TIMEOUT		10

#define RTW_USB_RXCB_NUM		4
#define RTW_USB_RX_SKB_NUM		8

#define RTW_USB_EP_MAX			4

#define TX_DESC_QSEL_MAX		20

#define RTW_USB_VENDOR_ID_REALTEK	0x0bda

static inline struct rtw_usb *rtw_get_usb_priv(struct rtw_dev *rtwdev)
{
	return (struct rtw_usb *)rtwdev->priv;
}

struct rx_usb_ctrl_block {
	struct rtw_dev *rtwdev;
	struct urb *rx_urb;
	struct sk_buff *rx_skb;
};

struct rtw_usb_tx_data {
	u8 sn;
};

struct rtw_usb {
	struct rtw_dev *rtwdev;
	struct usb_device *udev;

	/* protects usb_data_index */
	spinlock_t usb_lock;
	__le32 *usb_data;
	unsigned int usb_data_index;

	u8 pipe_interrupt;
	u8 pipe_in;
	u8 out_ep[RTW_USB_EP_MAX];
	int qsel_to_ep[TX_DESC_QSEL_MAX];

	struct workqueue_struct *txwq, *rxwq;

	struct sk_buff_head tx_queue[RTW_USB_EP_MAX];
	struct work_struct tx_work;

	struct rx_usb_ctrl_block rx_cb[RTW_USB_RXCB_NUM];
	struct sk_buff_head rx_queue;
	struct sk_buff_head rx_free_queue;
	struct work_struct rx_work;
	struct work_struct rx_urb_work;
};

static inline struct rtw_usb_tx_data *rtw_usb_get_tx_data(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	BUILD_BUG_ON(sizeof(struct rtw_usb_tx_data) >
		     sizeof(info->status.status_driver_data));

	return (struct rtw_usb_tx_data *)info->status.status_driver_data;
}

int rtw_usb_probe(struct usb_interface *intf, const struct usb_device_id *id);
void rtw_usb_disconnect(struct usb_interface *intf);

#endif
