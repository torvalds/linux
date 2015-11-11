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

#ifndef _GDM_MUX_H_
#define _GDM_MUX_H_

#include <linux/types.h>
#include <linux/usb.h>
#include <linux/list.h>

#include "gdm_tty.h"

#define PM_NORMAL 0
#define PM_SUSPEND 1

#define USB_RT_ACM          (USB_TYPE_CLASS | USB_RECIP_INTERFACE)

#define START_FLAG 0xA512485A
#define MUX_HEADER_SIZE 14
#define MUX_TX_MAX_SIZE (1024*10)
#define MUX_RX_MAX_SIZE (1024*30)
#define AT_PKT_TYPE 0xF011
#define DM_PKT_TYPE 0xF010

#define RETRY_TIMER 30 /* msec */

struct mux_pkt_header {
	__le32 start_flag;
	__le32 seq_num;
	__le32 payload_size;
	__le16 packet_type;
	unsigned char data[0];
};

struct mux_tx {
	struct urb *urb;
	u8 *buf;
	int  len;
	void (*callback)(void *cb_data);
	void *cb_data;
};

struct mux_rx {
	struct list_head free_list;
	struct list_head rx_submit_list;
	struct list_head to_host_list;
	struct urb *urb;
	u8 *buf;
	void *mux_dev;
	u32 offset;
	u32 len;
	int (*callback)(void *data,
			int len,
			int tty_index,
			struct tty_dev *tty_dev,
			int complete);
};

struct rx_cxt {
	struct list_head to_host_list;
	struct list_head rx_submit_list;
	struct list_head rx_free_list;
	spinlock_t to_host_lock;
	spinlock_t submit_list_lock;
	spinlock_t free_list_lock;
};

struct mux_dev {
	struct usb_device *usbdev;
	struct usb_interface *control_intf;
	struct usb_interface *data_intf;
	struct rx_cxt	rx;
	struct delayed_work work_rx;
	struct usb_interface *intf;
	int usb_state;
	int (*rx_cb)(void *data,
		     int len,
		     int tty_index,
		     struct tty_dev *tty_dev,
		     int complete);
	spinlock_t write_lock;
	struct tty_dev *tty_dev;
};

#endif /* _GDM_MUX_H_ */
