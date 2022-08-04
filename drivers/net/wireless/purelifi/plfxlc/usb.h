/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 pureLiFi
 */

#ifndef PLFXLC_USB_H
#define PLFXLC_USB_H

#include <linux/completion.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/usb.h>

#include "intf.h"

#define USB_BULK_MSG_TIMEOUT_MS 2000

#define PURELIFI_X_VENDOR_ID_0   0x16C1
#define PURELIFI_X_PRODUCT_ID_0  0x1CDE
#define PURELIFI_XC_VENDOR_ID_0  0x2EF5
#define PURELIFI_XC_PRODUCT_ID_0 0x0008
#define PURELIFI_XL_VENDOR_ID_0  0x2EF5
#define PURELIFI_XL_PRODUCT_ID_0 0x000A /* Station */

#define PLF_FPGA_STATUS_LEN 2
#define PLF_FPGA_STATE_LEN 9
#define PLF_BULK_TLEN 16384
#define PLF_FPGA_MG 6 /* Magic check */
#define PLF_XL_BUF_LEN 64
#define PLF_MSG_STATUS_OFFSET 7

#define PLF_USB_TIMEOUT 1000
#define PLF_MSLEEP_TIME 200

#define PURELIFI_URB_RETRY_MAX 5

#define plfxlc_usb_dev(usb) (&(usb)->intf->dev)

/* Tx retry backoff timer (in milliseconds) */
#define TX_RETRY_BACKOFF_MS 10
#define STA_QUEUE_CLEANUP_MS 5000

/* Tx retry backoff timer (in jiffies) */
#define TX_RETRY_BACKOFF_JIFF ((TX_RETRY_BACKOFF_MS * HZ) / 1000)
#define STA_QUEUE_CLEANUP_JIFF ((STA_QUEUE_CLEANUP_MS * HZ) / 1000)

/* Ensures that MAX_TRANSFER_SIZE is even. */
#define MAX_TRANSFER_SIZE (USB_MAX_TRANSFER_SIZE & ~1)
#define plfxlc_urb_dev(urb) (&(urb)->dev->dev)

#define STATION_FIFO_ALMOST_FULL_MESSAGE     0
#define STATION_FIFO_ALMOST_FULL_NOT_MESSAGE 1
#define STATION_CONNECT_MESSAGE              2
#define STATION_DISCONNECT_MESSAGE           3

int plfxlc_usb_wreq(struct usb_interface *ez_usb, void *buffer, int buffer_len,
		    enum plf_usb_req_enum usb_req_id);
void plfxlc_tx_urb_complete(struct urb *urb);

enum {
	USB_MAX_RX_SIZE       = 4800,
	USB_MAX_EP_INT_BUFFER = 64,
};

struct plfxlc_usb_interrupt {
	spinlock_t lock; /* spin lock for usb interrupt buffer */
	struct urb *urb;
	void *buffer;
	int interval;
};

#define RX_URBS_COUNT 5

struct plfxlc_usb_rx {
	spinlock_t lock; /* spin lock for rx urb */
	struct mutex setup_mutex; /* mutex lockt for rx urb */
	u8 fragment[2 * USB_MAX_RX_SIZE];
	unsigned int fragment_length;
	unsigned int usb_packet_size;
	struct urb **urbs;
	int urbs_count;
};

struct plf_station {
   /*  7...3    |    2      |     1     |     0	    |
    * Reserved  | Heartbeat | FIFO full | Connected |
    */
	unsigned char flag;
	unsigned char mac[ETH_ALEN];
	struct sk_buff_head data_list;
};

struct plfxlc_firmware_file {
	u32 total_files;
	u32 total_size;
	u32 size;
	u32 start_addr;
	u32 control_packets;
} __packed;

#define STATION_CONNECTED_FLAG 0x1
#define STATION_FIFO_FULL_FLAG 0x2
#define STATION_HEARTBEAT_FLAG 0x4
#define STATION_ACTIVE_FLAG    0xFD

#define PURELIFI_SERIAL_LEN 256
#define STA_BROADCAST_INDEX (AP_USER_LIMIT)
#define MAX_STA_NUM         (AP_USER_LIMIT + 1)

struct plfxlc_usb_tx {
	unsigned long enabled;
	spinlock_t lock; /* spinlock for USB tx */
	u8 mac_fifo_full;
	struct sk_buff_head submitted_skbs;
	struct usb_anchor submitted;
	int submitted_urbs;
	bool stopped;
	struct timer_list tx_retry_timer;
	struct plf_station station[MAX_STA_NUM];
};

/* Contains the usb parts. The structure doesn't require a lock because intf
 * will not be changed after initialization.
 */
struct plfxlc_usb {
	struct timer_list sta_queue_cleanup;
	struct plfxlc_usb_rx rx;
	struct plfxlc_usb_tx tx;
	struct usb_interface *intf;
	struct usb_interface *ez_usb;
	u8 req_buf[64]; /* plfxlc_usb_iowrite16v needs 62 bytes */
	u8 sidx; /* store last served */
	bool rx_usb_enabled;
	bool initialized;
	bool was_running;
	bool link_up;
};

enum endpoints {
	EP_DATA_IN  = 2,
	EP_DATA_OUT = 8,
};

enum devicetype {
	DEVICE_LIFI_X  = 0,
	DEVICE_LIFI_XC  = 1,
	DEVICE_LIFI_XL  = 1,
};

enum {
	PLF_BIT_ENABLED = 1,
	PLF_BIT_MAX = 2,
};

int plfxlc_usb_wreq_async(struct plfxlc_usb *usb, const u8 *buffer,
			  int buffer_len, enum plf_usb_req_enum usb_req_id,
			  usb_complete_t complete_fn, void *context);

static inline struct usb_device *
plfxlc_usb_to_usbdev(struct plfxlc_usb *usb)
{
	return interface_to_usbdev(usb->intf);
}

static inline struct ieee80211_hw *
plfxlc_intf_to_hw(struct usb_interface *intf)
{
	return usb_get_intfdata(intf);
}

static inline struct ieee80211_hw *
plfxlc_usb_to_hw(struct plfxlc_usb *usb)
{
	return plfxlc_intf_to_hw(usb->intf);
}

void plfxlc_usb_init(struct plfxlc_usb *usb, struct ieee80211_hw *hw,
		     struct usb_interface *intf);
void plfxlc_send_packet_from_data_queue(struct plfxlc_usb *usb);
void plfxlc_usb_release(struct plfxlc_usb *usb);
void plfxlc_usb_disable_rx(struct plfxlc_usb *usb);
void plfxlc_usb_enable_tx(struct plfxlc_usb *usb);
void plfxlc_usb_disable_tx(struct plfxlc_usb *usb);
int plfxlc_usb_tx(struct plfxlc_usb *usb, struct sk_buff *skb);
int plfxlc_usb_enable_rx(struct plfxlc_usb *usb);
int plfxlc_usb_init_hw(struct plfxlc_usb *usb);
const char *plfxlc_speed(enum usb_device_speed speed);

/* Firmware declarations */
int plfxlc_download_xl_firmware(struct usb_interface *intf);
int plfxlc_download_fpga(struct usb_interface *intf);

int plfxlc_upload_mac_and_serial(struct usb_interface *intf,
				 unsigned char *hw_address,
				 unsigned char *serial_number);

#endif /* PLFXLC_USB_H */
