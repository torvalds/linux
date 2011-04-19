/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _ZD_USB_H
#define _ZD_USB_H

#include <linux/completion.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/usb.h>

#include "zd_def.h"

#define ZD_USB_TX_HIGH  5
#define ZD_USB_TX_LOW   2

#define ZD_TX_TIMEOUT		(HZ * 5)
#define ZD_TX_WATCHDOG_INTERVAL	round_jiffies_relative(HZ)
#define ZD_RX_IDLE_INTERVAL	round_jiffies_relative(30 * HZ)

enum devicetype {
	DEVICE_ZD1211  = 0,
	DEVICE_ZD1211B = 1,
	DEVICE_INSTALLER = 2,
};

enum endpoints {
	EP_CTRL	    = 0,
	EP_DATA_OUT = 1,
	EP_DATA_IN  = 2,
	EP_INT_IN   = 3,
	EP_REGS_OUT = 4,
};

enum {
	USB_MAX_TRANSFER_SIZE		= 4096, /* bytes */
	/* FIXME: The original driver uses this value. We have to check,
	 * whether the MAX_TRANSFER_SIZE is sufficient and this needs only be
	 * used if one combined frame is split over two USB transactions.
	 */
	USB_MAX_RX_SIZE			= 4800, /* bytes */
	USB_MAX_IOWRITE16_COUNT		= 15,
	USB_MAX_IOWRITE32_COUNT		= USB_MAX_IOWRITE16_COUNT/2,
	USB_MAX_IOREAD16_COUNT		= 15,
	USB_MAX_IOREAD32_COUNT		= USB_MAX_IOREAD16_COUNT/2,
	USB_MIN_RFWRITE_BIT_COUNT	= 16,
	USB_MAX_RFWRITE_BIT_COUNT	= 28,
	USB_MAX_EP_INT_BUFFER		= 64,
	USB_ZD1211B_BCD_DEVICE		= 0x4810,
};

enum control_requests {
	USB_REQ_WRITE_REGS		= 0x21,
	USB_REQ_READ_REGS		= 0x22,
	USB_REQ_WRITE_RF		= 0x23,
	USB_REQ_PROG_FLASH		= 0x24,
	USB_REQ_EEPROM_START		= 0x0128, /* ? request is a byte */
	USB_REQ_EEPROM_MID		= 0x28,
	USB_REQ_EEPROM_END		= 0x0228, /* ? request is a byte */
	USB_REQ_FIRMWARE_DOWNLOAD	= 0x30,
	USB_REQ_FIRMWARE_CONFIRM	= 0x31,
	USB_REQ_FIRMWARE_READ_DATA	= 0x32,
};

struct usb_req_read_regs {
	__le16 id;
	__le16 addr[0];
} __packed;

struct reg_data {
	__le16 addr;
	__le16 value;
} __packed;

struct usb_req_write_regs {
	__le16 id;
	struct reg_data reg_writes[0];
} __packed;

enum {
	RF_IF_LE = 0x02,
	RF_CLK   = 0x04,
	RF_DATA	 = 0x08,
};

struct usb_req_rfwrite {
	__le16 id;
	__le16 value;
	/* 1: 3683a */
	/* 2: other (default) */
	__le16 bits;
	/* RF2595: 24 */
	__le16 bit_values[0];
	/* (CR203 & ~(RF_IF_LE | RF_CLK | RF_DATA)) | (bit ? RF_DATA : 0) */
} __packed;

/* USB interrupt */

enum usb_int_id {
	USB_INT_TYPE			= 0x01,
	USB_INT_ID_REGS			= 0x90,
	USB_INT_ID_RETRY_FAILED		= 0xa0,
};

enum usb_int_flags {
	USB_INT_READ_REGS_EN		= 0x01,
};

struct usb_int_header {
	u8 type;	/* must always be 1 */
	u8 id;
} __packed;

struct usb_int_regs {
	struct usb_int_header hdr;
	struct reg_data regs[0];
} __packed;

struct usb_int_retry_fail {
	struct usb_int_header hdr;
	u8 new_rate;
	u8 _dummy;
	u8 addr[ETH_ALEN];
	u8 ibss_wakeup_dest;
} __packed;

struct read_regs_int {
	struct completion completion;
	/* Stores the USB int structure and contains the USB address of the
	 * first requested register before request.
	 */
	u8 buffer[USB_MAX_EP_INT_BUFFER];
	int length;
	__le16 cr_int_addr;
};

struct zd_ioreq16 {
	zd_addr_t addr;
	u16 value;
};

struct zd_ioreq32 {
	zd_addr_t addr;
	u32 value;
};

struct zd_usb_interrupt {
	struct read_regs_int read_regs;
	spinlock_t lock;
	struct urb *urb;
	void *buffer;
	dma_addr_t buffer_dma;
	int interval;
	u8 read_regs_enabled:1;
};

static inline struct usb_int_regs *get_read_regs(struct zd_usb_interrupt *intr)
{
	return (struct usb_int_regs *)intr->read_regs.buffer;
}

#define RX_URBS_COUNT 5

struct zd_usb_rx {
	spinlock_t lock;
	struct mutex setup_mutex;
	struct delayed_work idle_work;
	struct tasklet_struct reset_timer_tasklet;
	u8 fragment[2 * USB_MAX_RX_SIZE];
	unsigned int fragment_length;
	unsigned int usb_packet_size;
	struct urb **urbs;
	int urbs_count;
};

/**
 * struct zd_usb_tx - structure used for transmitting frames
 * @enabled: atomic enabled flag, indicates whether tx is enabled
 * @lock: lock for transmission
 * @submitted: anchor for URBs sent to device
 * @submitted_urbs: atomic integer that counts the URBs having sent to the
 *	device, which haven't been completed
 * @stopped: indicates whether higher level tx queues are stopped
 */
struct zd_usb_tx {
	atomic_t enabled;
	spinlock_t lock;
	struct delayed_work watchdog_work;
	struct sk_buff_head submitted_skbs;
	struct usb_anchor submitted;
	int submitted_urbs;
	u8 stopped:1, watchdog_enabled:1;
};

/* Contains the usb parts. The structure doesn't require a lock because intf
 * will not be changed after initialization.
 */
struct zd_usb {
	struct zd_usb_interrupt intr;
	struct zd_usb_rx rx;
	struct zd_usb_tx tx;
	struct usb_interface *intf;
	struct usb_anchor submitted_cmds;
	struct urb *urb_async_waiting;
	int cmd_error;
	u8 req_buf[64]; /* zd_usb_iowrite16v needs 62 bytes */
	u8 is_zd1211b:1, initialized:1, was_running:1, in_async:1;
};

#define zd_usb_dev(usb) (&usb->intf->dev)

static inline struct usb_device *zd_usb_to_usbdev(struct zd_usb *usb)
{
	return interface_to_usbdev(usb->intf);
}

static inline struct ieee80211_hw *zd_intf_to_hw(struct usb_interface *intf)
{
	return usb_get_intfdata(intf);
}

static inline struct ieee80211_hw *zd_usb_to_hw(struct zd_usb *usb)
{
	return zd_intf_to_hw(usb->intf);
}

void zd_usb_init(struct zd_usb *usb, struct ieee80211_hw *hw,
	         struct usb_interface *intf);
int zd_usb_init_hw(struct zd_usb *usb);
void zd_usb_clear(struct zd_usb *usb);

int zd_usb_scnprint_id(struct zd_usb *usb, char *buffer, size_t size);

void zd_tx_watchdog_enable(struct zd_usb *usb);
void zd_tx_watchdog_disable(struct zd_usb *usb);

int zd_usb_enable_int(struct zd_usb *usb);
void zd_usb_disable_int(struct zd_usb *usb);

int zd_usb_enable_rx(struct zd_usb *usb);
void zd_usb_disable_rx(struct zd_usb *usb);

void zd_usb_reset_rx_idle_timer(struct zd_usb *usb);

void zd_usb_enable_tx(struct zd_usb *usb);
void zd_usb_disable_tx(struct zd_usb *usb);

int zd_usb_tx(struct zd_usb *usb, struct sk_buff *skb);

int zd_usb_ioread16v(struct zd_usb *usb, u16 *values,
	         const zd_addr_t *addresses, unsigned int count);

static inline int zd_usb_ioread16(struct zd_usb *usb, u16 *value,
	                      const zd_addr_t addr)
{
	return zd_usb_ioread16v(usb, value, (const zd_addr_t *)&addr, 1);
}

void zd_usb_iowrite16v_async_start(struct zd_usb *usb);
int zd_usb_iowrite16v_async_end(struct zd_usb *usb, unsigned int timeout);
int zd_usb_iowrite16v_async(struct zd_usb *usb, const struct zd_ioreq16 *ioreqs,
			    unsigned int count);
int zd_usb_iowrite16v(struct zd_usb *usb, const struct zd_ioreq16 *ioreqs,
	              unsigned int count);

int zd_usb_rfwrite(struct zd_usb *usb, u32 value, u8 bits);

int zd_usb_read_fw(struct zd_usb *usb, zd_addr_t addr, u8 *data, u16 len);

extern struct workqueue_struct *zd_workqueue;

#endif /* _ZD_USB_H */
