/*
 * This file contains definitions for mwifiex USB interface driver.
 *
 * Copyright 2011-2020 NXP
 *
 * This software file (the "File") is distributed by NXP
 * under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#ifndef _MWIFIEX_USB_H
#define _MWIFIEX_USB_H

#include <linux/completion.h>
#include <linux/usb.h>

#define USB8XXX_VID		0x1286

#define USB8766_PID_1		0x2041
#define USB8766_PID_2		0x2042
#define USB8797_PID_1		0x2043
#define USB8797_PID_2		0x2044
#define USB8801_PID_1		0x2049
#define USB8801_PID_2		0x204a
#define USB8997_PID_1		0x2052
#define USB8997_PID_2		0x204e


#define USB8XXX_FW_DNLD		1
#define USB8XXX_FW_READY	2
#define USB8XXX_FW_MAX_RETRY	3

#define MWIFIEX_TX_DATA_PORT	2
#define MWIFIEX_TX_DATA_URB	6
#define MWIFIEX_RX_DATA_URB	6
#define MWIFIEX_USB_TIMEOUT	100

#define USB8766_DEFAULT_FW_NAME	"mrvl/usb8766_uapsta.bin"
#define USB8797_DEFAULT_FW_NAME	"mrvl/usb8797_uapsta.bin"
#define USB8801_DEFAULT_FW_NAME	"mrvl/usb8801_uapsta.bin"
#define USB8997_DEFAULT_FW_NAME	"mrvl/usbusb8997_combo_v4.bin"

#define FW_DNLD_TX_BUF_SIZE	620
#define FW_DNLD_RX_BUF_SIZE	2048
#define FW_HAS_LAST_BLOCK	0x00000004
#define FW_CMD_7		0x00000007

#define FW_DATA_XMIT_SIZE \
	(sizeof(struct fw_header) + dlen + sizeof(u32))

struct urb_context {
	struct mwifiex_adapter *adapter;
	struct sk_buff *skb;
	struct urb *urb;
	u8 ep;
};

#define MWIFIEX_USB_TX_AGGR_TMO_MIN	1
#define MWIFIEX_USB_TX_AGGR_TMO_MAX	4

struct tx_aggr_tmr_cnxt {
	struct mwifiex_adapter *adapter;
	struct usb_tx_data_port *port;
	struct timer_list hold_timer;
	bool is_hold_timer_set;
	u32 hold_tmo_msecs;
};

struct usb_tx_aggr {
	struct sk_buff_head aggr_list;
	int aggr_len;
	int aggr_num;
	struct tx_aggr_tmr_cnxt timer_cnxt;
};

struct usb_tx_data_port {
	u8 tx_data_ep;
	u8 block_status;
	atomic_t tx_data_urb_pending;
	int tx_data_ix;
	struct urb_context tx_data_list[MWIFIEX_TX_DATA_URB];
	/* usb tx aggregation*/
	struct usb_tx_aggr tx_aggr;
	struct sk_buff *skb_aggr[MWIFIEX_TX_DATA_URB];
	/* lock for protect tx aggregation data path*/
	spinlock_t tx_aggr_lock;
};

struct usb_card_rec {
	struct mwifiex_adapter *adapter;
	struct usb_device *udev;
	struct usb_interface *intf;
	struct completion fw_done;
	u8 rx_cmd_ep;
	struct urb_context rx_cmd;
	atomic_t rx_cmd_urb_pending;
	struct urb_context rx_data_list[MWIFIEX_RX_DATA_URB];
	u8 usb_boot_state;
	u8 rx_data_ep;
	atomic_t rx_data_urb_pending;
	u8 tx_cmd_ep;
	atomic_t tx_cmd_urb_pending;
	int bulk_out_maxpktsize;
	struct urb_context tx_cmd;
	u8 mc_resync_flag;
	struct usb_tx_data_port port[MWIFIEX_TX_DATA_PORT];
	int rx_cmd_ep_type;
	u8 rx_cmd_interval;
	int tx_cmd_ep_type;
	u8 tx_cmd_interval;
};

struct fw_header {
	__le32 dnld_cmd;
	__le32 base_addr;
	__le32 data_len;
	__le32 crc;
};

struct fw_sync_header {
	__le32 cmd;
	__le32 seq_num;
} __packed;

struct fw_data {
	struct fw_header fw_hdr;
	__le32 seq_num;
	u8 data[1];
} __packed;

#endif /*_MWIFIEX_USB_H */
