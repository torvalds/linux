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

#ifndef __GDM_USB_H__
#define __GDM_USB_H__

#include <linux/types.h>
#include <linux/usb.h>
#include <linux/list.h>

#define B_DIFF_DL_DRV		(1 << 4)
#define B_DOWNLOAD		(1 << 5)
#define MAX_NR_SDU_BUF		64

struct usb_tx {
	struct list_head	list;
#if defined(CONFIG_WIMAX_GDM72XX_USB_PM) || defined(CONFIG_WIMAX_GDM72XX_K_MODE)
	struct list_head	p_list;
#endif
	struct tx_cxt		*tx_cxt;
	struct urb		*urb;
	u8			*buf;
	void (*callback)(void *cb_data);
	void			*cb_data;
};

struct tx_cxt {
	struct list_head	free_list;
	struct list_head	sdu_list;
	struct list_head	hci_list;
#if defined(CONFIG_WIMAX_GDM72XX_USB_PM) || defined(CONFIG_WIMAX_GDM72XX_K_MODE)
	struct list_head	pending_list;
#endif
	spinlock_t		lock;
};

struct usb_rx {
	struct list_head	list;
	struct rx_cxt		*rx_cxt;
	struct urb		*urb;
	u8			*buf;
	void (*callback)(void *cb_data, void *data, int len);
	void *cb_data;
};

struct rx_cxt {
	struct list_head	free_list;
	struct list_head	used_list;
	spinlock_t		lock;
};

struct usbwm_dev {
	struct usb_device	*usbdev;
#ifdef CONFIG_WIMAX_GDM72XX_USB_PM
	struct work_struct	pm_ws;

	struct usb_interface	*intf;
#endif
#ifdef CONFIG_WIMAX_GDM72XX_K_MODE
	int bw_switch;
	struct list_head	list;
#endif
	struct tx_cxt		tx;
	struct rx_cxt		rx;
	int			padding;
};

#endif /* __GDM_USB_H__ */
