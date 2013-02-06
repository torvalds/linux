/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#ifndef __LINUX_USB_BRIDGE_H__
#define __LINUX_USB_BRIDGE_H__

#include <linux/netdevice.h>
#include <linux/usb.h>

/* bridge device 0: DUN
 * bridge device 1 : Tethered RMNET
 */
#define MAX_BRIDGE_DEVICES 2

struct bridge_ops {
	int (*send_pkt)(void *, void *, size_t actual);
	void (*send_cbits)(void *, unsigned int);

	/* flow control */
	void (*unthrottle_tx)(void *);
};

#define TX_THROTTLED BIT(0)
#define RX_THROTTLED BIT(1)

struct bridge {
	/* context of the gadget port using bridge driver */
	void *ctx;

	/* bridge device array index mapped to the gadget port array index.
	 * data bridge[ch_id] <-- bridge --> gadget port[ch_id]
	 */
	unsigned int ch_id;

	/* flow control bits */
	unsigned long flags;

	/* data/ctrl bridge callbacks */
	struct bridge_ops ops;
};

/**
 * timestamp_info: stores timestamp info for skb life cycle during data
 * transfer for tethered rmnet/DUN.
 * @created: stores timestamp at the time of creation of SKB.
 * @rx_queued: stores timestamp when SKB queued to HW to receive
 * data.
 * @rx_done: stores timestamp when skb queued to h/w is completed.
 * @rx_done_sent: stores timestamp when SKB is sent from gadget rmnet/DUN
 * driver to bridge rmnet/DUN driver or vice versa.
 * @tx_queued: stores timestamp when SKB is queued to send data.
 *
 * note that size of this struct shouldnt exceed 48bytes that's the max skb->cb
 * holds.
 */
struct timestamp_info {
	struct data_bridge	*dev;

	unsigned int		created;
	unsigned int		rx_queued;
	unsigned int		rx_done;
	unsigned int		rx_done_sent;
	unsigned int		tx_queued;
};

/* Maximum timestamp message length */
#define DBG_DATA_MSG	128UL

/* Maximum timestamp messages */
#define DBG_DATA_MAX	32UL

/* timestamp buffer descriptor */
struct timestamp_buf {
	char		(buf[DBG_DATA_MAX])[DBG_DATA_MSG];   /* buffer */
	unsigned	idx;   /* index */
	rwlock_t	lck;   /* lock */
};

#if defined(CONFIG_USB_QCOM_MDM_BRIDGE) ||	\
	defined(CONFIG_USB_QCOM_MDM_BRIDGE_MODULE)

/* Bridge APIs called by gadget driver */
int ctrl_bridge_open(struct bridge *);
void ctrl_bridge_close(unsigned int);
int ctrl_bridge_write(unsigned int, char *, size_t);
int ctrl_bridge_set_cbits(unsigned int, unsigned int);
unsigned int ctrl_bridge_get_cbits_tohost(unsigned int);
int data_bridge_open(struct bridge *brdg);
void data_bridge_close(unsigned int);
int data_bridge_write(unsigned int , struct sk_buff *);
int data_bridge_unthrottle_rx(unsigned int);

/* defined in control bridge */
int ctrl_bridge_probe(struct usb_interface *, struct usb_host_endpoint *, int);
void ctrl_bridge_disconnect(unsigned int);
int ctrl_bridge_resume(unsigned int);
int ctrl_bridge_suspend(unsigned int);

#else

static inline int __maybe_unused ctrl_bridge_open(struct bridge *brdg)
{
	return -ENODEV;
}

static inline void __maybe_unused ctrl_bridge_close(unsigned int id) { }

static inline int __maybe_unused ctrl_bridge_write(unsigned int id,
						char *data, size_t size)
{
	return -ENODEV;
}

static inline int __maybe_unused ctrl_bridge_set_cbits(unsigned int id,
					unsigned int cbits)
{
	return -ENODEV;
}

static inline unsigned int __maybe_unused
ctrl_bridge_get_cbits_tohost(unsigned int id)
{
	return -ENODEV;
}

static inline int __maybe_unused data_bridge_open(struct bridge *brdg)
{
	return -ENODEV;
}

static inline void __maybe_unused data_bridge_close(unsigned int id) { }

static inline int __maybe_unused data_bridge_write(unsigned int id,
					    struct sk_buff *skb)
{
	return -ENODEV;
}

static inline int __maybe_unused data_bridge_unthrottle_rx(unsigned int id)
{
	return -ENODEV;
}

#endif

#endif
