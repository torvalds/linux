/* SPDX-License-Identifier: GPL-2.0 */
/* Parts of this driver are based on the following:
 *  - Kvaser linux leaf driver (version 4.78)
 *  - CAN driver for esd CAN-USB/2
 *  - Kvaser linux usbcanII driver (version 5.3)
 *  - Kvaser linux mhydra driver (version 5.24)
 *
 * Copyright (C) 2002-2018 KVASER AB, Sweden. All rights reserved.
 * Copyright (C) 2010 Matthias Fuchs <matthias.fuchs@esd.eu>, esd gmbh
 * Copyright (C) 2012 Olivier Sobrie <olivier@sobrie.be>
 * Copyright (C) 2015 Valeo S.A.
 */

#ifndef KVASER_USB_H
#define KVASER_USB_H

/* Kvaser USB CAN dongles are divided into three major platforms:
 * - Hydra: Running firmware labeled as 'mhydra'
 * - Leaf: Based on Renesas M32C or Freescale i.MX28, running firmware labeled
 *         as 'filo'
 * - UsbcanII: Based on Renesas M16C, running firmware labeled as 'helios'
 */

#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/usb.h>

#include <linux/can.h>
#include <linux/can/dev.h>

#define KVASER_USB_MAX_RX_URBS			4
#define KVASER_USB_MAX_TX_URBS			128
#define KVASER_USB_TIMEOUT			1000 /* msecs */
#define KVASER_USB_RX_BUFFER_SIZE		3072
#define KVASER_USB_MAX_NET_DEVICES		5

/* USB devices features */
#define KVASER_USB_HAS_SILENT_MODE		BIT(0)
#define KVASER_USB_HAS_TXRX_ERRORS		BIT(1)

/* Device capabilities */
#define KVASER_USB_CAP_BERR_CAP			0x01
#define KVASER_USB_CAP_EXT_CAP			0x02
#define KVASER_USB_HYDRA_CAP_EXT_CMD		0x04

struct kvaser_usb_dev_cfg;

enum kvaser_usb_leaf_family {
	KVASER_LEAF,
	KVASER_USBCAN,
};

#define KVASER_USB_HYDRA_MAX_CMD_LEN		128
struct kvaser_usb_dev_card_data_hydra {
	u8 channel_to_he[KVASER_USB_MAX_NET_DEVICES];
	u8 sysdbg_he;
	spinlock_t transid_lock; /* lock for transid */
	u16 transid;
	/* lock for usb_rx_leftover and usb_rx_leftover_len */
	spinlock_t usb_rx_leftover_lock;
	u8 usb_rx_leftover[KVASER_USB_HYDRA_MAX_CMD_LEN];
	u8 usb_rx_leftover_len;
};
struct kvaser_usb_dev_card_data {
	u32 ctrlmode_supported;
	u32 capabilities;
	union {
		struct {
			enum kvaser_usb_leaf_family family;
		} leaf;
		struct kvaser_usb_dev_card_data_hydra hydra;
	};
};

/* Context for an outstanding, not yet ACKed, transmission */
struct kvaser_usb_tx_urb_context {
	struct kvaser_usb_net_priv *priv;
	u32 echo_index;
};

struct kvaser_usb {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct kvaser_usb_net_priv *nets[KVASER_USB_MAX_NET_DEVICES];
	const struct kvaser_usb_dev_ops *ops;
	const struct kvaser_usb_dev_cfg *cfg;

	struct usb_endpoint_descriptor *bulk_in, *bulk_out;
	struct usb_anchor rx_submitted;

	/* @max_tx_urbs: Firmware-reported maximum number of outstanding,
	 * not yet ACKed, transmissions on this device. This value is
	 * also used as a sentinel for marking free tx contexts.
	 */
	u32 fw_version;
	unsigned int nchannels;
	unsigned int max_tx_urbs;
	struct kvaser_usb_dev_card_data card_data;

	bool rxinitdone;
	void *rxbuf[KVASER_USB_MAX_RX_URBS];
	dma_addr_t rxbuf_dma[KVASER_USB_MAX_RX_URBS];
};

struct kvaser_usb_net_priv {
	struct can_priv can;
	struct can_berr_counter bec;

	struct kvaser_usb *dev;
	struct net_device *netdev;
	int channel;

	struct completion start_comp, stop_comp, flush_comp;
	struct usb_anchor tx_submitted;

	spinlock_t tx_contexts_lock; /* lock for active_tx_contexts */
	int active_tx_contexts;
	struct kvaser_usb_tx_urb_context tx_contexts[];
};

/**
 * struct kvaser_usb_dev_ops - Device specific functions
 * @dev_set_mode:		used for can.do_set_mode
 * @dev_set_bittiming:		used for can.do_set_bittiming
 * @dev_set_data_bittiming:	used for can.do_set_data_bittiming
 * @dev_get_berr_counter:	used for can.do_get_berr_counter
 *
 * @dev_setup_endpoints:	setup USB in and out endpoints
 * @dev_init_card:		initialize card
 * @dev_get_software_info:	get software info
 * @dev_get_software_details:	get software details
 * @dev_get_card_info:		get card info
 * @dev_get_capabilities:	discover device capabilities
 *
 * @dev_set_opt_mode:		set ctrlmod
 * @dev_start_chip:		start the CAN controller
 * @dev_stop_chip:		stop the CAN controller
 * @dev_reset_chip:		reset the CAN controller
 * @dev_flush_queue:		flush outstanding CAN messages
 * @dev_read_bulk_callback:	handle incoming commands
 * @dev_frame_to_cmd:		translate struct can_frame into device command
 */
struct kvaser_usb_dev_ops {
	int (*dev_set_mode)(struct net_device *netdev, enum can_mode mode);
	int (*dev_set_bittiming)(struct net_device *netdev);
	int (*dev_set_data_bittiming)(struct net_device *netdev);
	int (*dev_get_berr_counter)(const struct net_device *netdev,
				    struct can_berr_counter *bec);
	int (*dev_setup_endpoints)(struct kvaser_usb *dev);
	int (*dev_init_card)(struct kvaser_usb *dev);
	int (*dev_get_software_info)(struct kvaser_usb *dev);
	int (*dev_get_software_details)(struct kvaser_usb *dev);
	int (*dev_get_card_info)(struct kvaser_usb *dev);
	int (*dev_get_capabilities)(struct kvaser_usb *dev);
	int (*dev_set_opt_mode)(const struct kvaser_usb_net_priv *priv);
	int (*dev_start_chip)(struct kvaser_usb_net_priv *priv);
	int (*dev_stop_chip)(struct kvaser_usb_net_priv *priv);
	int (*dev_reset_chip)(struct kvaser_usb *dev, int channel);
	int (*dev_flush_queue)(struct kvaser_usb_net_priv *priv);
	void (*dev_read_bulk_callback)(struct kvaser_usb *dev, void *buf,
				       int len);
	void *(*dev_frame_to_cmd)(const struct kvaser_usb_net_priv *priv,
				  const struct sk_buff *skb, int *cmd_len,
				  u16 transid);
};

struct kvaser_usb_dev_cfg {
	const struct can_clock clock;
	const unsigned int timestamp_freq;
	const struct can_bittiming_const * const bittiming_const;
	const struct can_bittiming_const * const data_bittiming_const;
};

extern const struct kvaser_usb_dev_ops kvaser_usb_hydra_dev_ops;
extern const struct kvaser_usb_dev_ops kvaser_usb_leaf_dev_ops;

int kvaser_usb_recv_cmd(const struct kvaser_usb *dev, void *cmd, int len,
			int *actual_len);

int kvaser_usb_send_cmd(const struct kvaser_usb *dev, void *cmd, int len);

int kvaser_usb_send_cmd_async(struct kvaser_usb_net_priv *priv, void *cmd,
			      int len);

int kvaser_usb_can_rx_over_error(struct net_device *netdev);
#endif /* KVASER_USB_H */
