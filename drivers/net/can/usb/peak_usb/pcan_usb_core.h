/*
 * CAN driver for PEAK System USB adapters
 * Derived from the PCAN project file driver/src/pcan_usb_core.c
 *
 * Copyright (C) 2003-2010 PEAK System-Technik GmbH
 * Copyright (C) 2010-2012 Stephane Grosjean <s.grosjean@peak-system.com>
 *
 * Many thanks to Klaus Hitschler <klaus.hitschler@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef PCAN_USB_CORE_H
#define PCAN_USB_CORE_H

/* PEAK-System vendor id. */
#define PCAN_USB_VENDOR_ID		0x0c72

/* supported device ids. */
#define PCAN_USB_PRODUCT_ID		0x000c
#define PCAN_USBPRO_PRODUCT_ID		0x000d
#define PCAN_USBPROFD_PRODUCT_ID	0x0011
#define PCAN_USBFD_PRODUCT_ID		0x0012
#define PCAN_USBCHIP_PRODUCT_ID		0x0013
#define PCAN_USBX6_PRODUCT_ID		0x0014

#define PCAN_USB_DRIVER_NAME		"peak_usb"

/* number of urbs that are submitted for rx/tx per channel */
#define PCAN_USB_MAX_RX_URBS		4
#define PCAN_USB_MAX_TX_URBS		10

/* usb adapters maximum channels per usb interface */
#define PCAN_USB_MAX_CHANNEL		2

/* maximum length of the usb commands sent to/received from  the devices */
#define PCAN_USB_MAX_CMD_LEN		32

struct peak_usb_device;

/* PEAK-System USB adapter descriptor */
struct peak_usb_adapter {
	char *name;
	u32 device_id;
	u32 ctrlmode_supported;
	struct can_clock clock;
	const struct can_bittiming_const * const bittiming_const;
	const struct can_bittiming_const * const data_bittiming_const;
	unsigned int ctrl_count;

	int (*intf_probe)(struct usb_interface *intf);

	int (*dev_init)(struct peak_usb_device *dev);
	void (*dev_exit)(struct peak_usb_device *dev);
	void (*dev_free)(struct peak_usb_device *dev);
	int (*dev_open)(struct peak_usb_device *dev);
	int (*dev_close)(struct peak_usb_device *dev);
	int (*dev_set_bittiming)(struct peak_usb_device *dev,
					struct can_bittiming *bt);
	int (*dev_set_data_bittiming)(struct peak_usb_device *dev,
				      struct can_bittiming *bt);
	int (*dev_set_bus)(struct peak_usb_device *dev, u8 onoff);
	int (*dev_get_device_id)(struct peak_usb_device *dev, u32 *device_id);
	int (*dev_decode_buf)(struct peak_usb_device *dev, struct urb *urb);
	int (*dev_encode_msg)(struct peak_usb_device *dev, struct sk_buff *skb,
					u8 *obuf, size_t *size);
	int (*dev_start)(struct peak_usb_device *dev);
	int (*dev_stop)(struct peak_usb_device *dev);
	int (*dev_restart_async)(struct peak_usb_device *dev, struct urb *urb,
					u8 *buf);
	int (*do_get_berr_counter)(const struct net_device *netdev,
				   struct can_berr_counter *bec);
	u8 ep_msg_in;
	u8 ep_msg_out[PCAN_USB_MAX_CHANNEL];
	u8 ts_used_bits;
	u32 ts_period;
	u8 us_per_ts_shift;
	u32 us_per_ts_scale;

	int rx_buffer_size;
	int tx_buffer_size;
	int sizeof_dev_private;
};

extern const struct peak_usb_adapter pcan_usb;
extern const struct peak_usb_adapter pcan_usb_pro;
extern const struct peak_usb_adapter pcan_usb_fd;
extern const struct peak_usb_adapter pcan_usb_chip;
extern const struct peak_usb_adapter pcan_usb_pro_fd;
extern const struct peak_usb_adapter pcan_usb_x6;

struct peak_time_ref {
	ktime_t tv_host_0, tv_host;
	u32 ts_dev_1, ts_dev_2;
	u64 ts_total;
	u32 tick_count;
	const struct peak_usb_adapter *adapter;
};

struct peak_tx_urb_context {
	struct peak_usb_device *dev;
	u32 echo_index;
	u8 data_len;
	struct urb *urb;
};

#define PCAN_USB_STATE_CONNECTED	0x00000001
#define PCAN_USB_STATE_STARTED		0x00000002

/* PEAK-System USB device */
struct peak_usb_device {
	struct can_priv can;
	const struct peak_usb_adapter *adapter;
	unsigned int ctrl_idx;
	u32 state;

	struct sk_buff *echo_skb[PCAN_USB_MAX_TX_URBS];

	struct usb_device *udev;
	struct net_device *netdev;

	atomic_t active_tx_urbs;
	struct usb_anchor tx_submitted;
	struct peak_tx_urb_context tx_contexts[PCAN_USB_MAX_TX_URBS];

	u8 *cmd_buf;
	struct usb_anchor rx_submitted;

	u32 device_number;
	u8 device_rev;

	u8 ep_msg_in;
	u8 ep_msg_out;

	u16 bus_load;

	struct peak_usb_device *prev_siblings;
	struct peak_usb_device *next_siblings;
};

void pcan_dump_mem(char *prompt, void *p, int l);

/* common timestamp management */
void peak_usb_init_time_ref(struct peak_time_ref *time_ref,
			    const struct peak_usb_adapter *adapter);
void peak_usb_update_ts_now(struct peak_time_ref *time_ref, u32 ts_now);
void peak_usb_set_ts_now(struct peak_time_ref *time_ref, u32 ts_now);
void peak_usb_get_ts_time(struct peak_time_ref *time_ref, u32 ts, ktime_t *tv);
int peak_usb_netif_rx(struct sk_buff *skb,
		      struct peak_time_ref *time_ref, u32 ts_low);
void peak_usb_async_complete(struct urb *urb);
void peak_usb_restart_complete(struct peak_usb_device *dev);

#endif
