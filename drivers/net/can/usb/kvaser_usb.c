/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * Parts of this driver are based on the following:
 *  - Kvaser linux leaf driver (version 4.78)
 *  - CAN driver for esd CAN-USB/2
 *  - Kvaser linux usbcanII driver (version 5.3)
 *
 * Copyright (C) 2002-2006 KVASER AB, Sweden. All rights reserved.
 * Copyright (C) 2010 Matthias Fuchs <matthias.fuchs@esd.eu>, esd gmbh
 * Copyright (C) 2012 Olivier Sobrie <olivier@sobrie.be>
 * Copyright (C) 2015 Valeo S.A.
 */

#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/usb.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

#define MAX_RX_URBS			4
#define START_TIMEOUT			1000 /* msecs */
#define STOP_TIMEOUT			1000 /* msecs */
#define USB_SEND_TIMEOUT		1000 /* msecs */
#define USB_RECV_TIMEOUT		1000 /* msecs */
#define RX_BUFFER_SIZE			3072
#define CAN_USB_CLOCK			8000000
#define MAX_NET_DEVICES			3
#define MAX_USBCAN_NET_DEVICES		2

/* Kvaser Leaf USB devices */
#define KVASER_VENDOR_ID		0x0bfd
#define USB_LEAF_DEVEL_PRODUCT_ID	10
#define USB_LEAF_LITE_PRODUCT_ID	11
#define USB_LEAF_PRO_PRODUCT_ID		12
#define USB_LEAF_SPRO_PRODUCT_ID	14
#define USB_LEAF_PRO_LS_PRODUCT_ID	15
#define USB_LEAF_PRO_SWC_PRODUCT_ID	16
#define USB_LEAF_PRO_LIN_PRODUCT_ID	17
#define USB_LEAF_SPRO_LS_PRODUCT_ID	18
#define USB_LEAF_SPRO_SWC_PRODUCT_ID	19
#define USB_MEMO2_DEVEL_PRODUCT_ID	22
#define USB_MEMO2_HSHS_PRODUCT_ID	23
#define USB_UPRO_HSHS_PRODUCT_ID	24
#define USB_LEAF_LITE_GI_PRODUCT_ID	25
#define USB_LEAF_PRO_OBDII_PRODUCT_ID	26
#define USB_MEMO2_HSLS_PRODUCT_ID	27
#define USB_LEAF_LITE_CH_PRODUCT_ID	28
#define USB_BLACKBIRD_SPRO_PRODUCT_ID	29
#define USB_OEM_MERCURY_PRODUCT_ID	34
#define USB_OEM_LEAF_PRODUCT_ID		35
#define USB_CAN_R_PRODUCT_ID		39
#define USB_LEAF_LITE_V2_PRODUCT_ID	288
#define USB_MINI_PCIE_HS_PRODUCT_ID	289
#define USB_LEAF_LIGHT_HS_V2_OEM_PRODUCT_ID 290
#define USB_USBCAN_LIGHT_2HS_PRODUCT_ID	291
#define USB_MINI_PCIE_2HS_PRODUCT_ID	292

static inline bool kvaser_is_leaf(const struct usb_device_id *id)
{
	return id->idProduct >= USB_LEAF_DEVEL_PRODUCT_ID &&
	       id->idProduct <= USB_MINI_PCIE_2HS_PRODUCT_ID;
}

/* Kvaser USBCan-II devices */
#define USB_USBCAN_REVB_PRODUCT_ID	2
#define USB_VCI2_PRODUCT_ID		3
#define USB_USBCAN2_PRODUCT_ID		4
#define USB_MEMORATOR_PRODUCT_ID	5

static inline bool kvaser_is_usbcan(const struct usb_device_id *id)
{
	return id->idProduct >= USB_USBCAN_REVB_PRODUCT_ID &&
	       id->idProduct <= USB_MEMORATOR_PRODUCT_ID;
}

/* USB devices features */
#define KVASER_HAS_SILENT_MODE		BIT(0)
#define KVASER_HAS_TXRX_ERRORS		BIT(1)

/* Message header size */
#define MSG_HEADER_LEN			2

/* Can message flags */
#define MSG_FLAG_ERROR_FRAME		BIT(0)
#define MSG_FLAG_OVERRUN		BIT(1)
#define MSG_FLAG_NERR			BIT(2)
#define MSG_FLAG_WAKEUP			BIT(3)
#define MSG_FLAG_REMOTE_FRAME		BIT(4)
#define MSG_FLAG_RESERVED		BIT(5)
#define MSG_FLAG_TX_ACK			BIT(6)
#define MSG_FLAG_TX_REQUEST		BIT(7)

/* Can states (M16C CxSTRH register) */
#define M16C_STATE_BUS_RESET		BIT(0)
#define M16C_STATE_BUS_ERROR		BIT(4)
#define M16C_STATE_BUS_PASSIVE		BIT(5)
#define M16C_STATE_BUS_OFF		BIT(6)

/* Can msg ids */
#define CMD_RX_STD_MESSAGE		12
#define CMD_TX_STD_MESSAGE		13
#define CMD_RX_EXT_MESSAGE		14
#define CMD_TX_EXT_MESSAGE		15
#define CMD_SET_BUS_PARAMS		16
#define CMD_GET_BUS_PARAMS		17
#define CMD_GET_BUS_PARAMS_REPLY	18
#define CMD_GET_CHIP_STATE		19
#define CMD_CHIP_STATE_EVENT		20
#define CMD_SET_CTRL_MODE		21
#define CMD_GET_CTRL_MODE		22
#define CMD_GET_CTRL_MODE_REPLY		23
#define CMD_RESET_CHIP			24
#define CMD_RESET_CARD			25
#define CMD_START_CHIP			26
#define CMD_START_CHIP_REPLY		27
#define CMD_STOP_CHIP			28
#define CMD_STOP_CHIP_REPLY		29

#define CMD_LEAF_GET_CARD_INFO2		32
#define CMD_USBCAN_RESET_CLOCK		32
#define CMD_USBCAN_CLOCK_OVERFLOW_EVENT	33

#define CMD_GET_CARD_INFO		34
#define CMD_GET_CARD_INFO_REPLY		35
#define CMD_GET_SOFTWARE_INFO		38
#define CMD_GET_SOFTWARE_INFO_REPLY	39
#define CMD_ERROR_EVENT			45
#define CMD_FLUSH_QUEUE			48
#define CMD_RESET_ERROR_COUNTER		49
#define CMD_TX_ACKNOWLEDGE		50
#define CMD_CAN_ERROR_EVENT		51
#define CMD_FLUSH_QUEUE_REPLY		68

#define CMD_LEAF_USB_THROTTLE		77
#define CMD_LEAF_LOG_MESSAGE		106

/* error factors */
#define M16C_EF_ACKE			BIT(0)
#define M16C_EF_CRCE			BIT(1)
#define M16C_EF_FORME			BIT(2)
#define M16C_EF_STFE			BIT(3)
#define M16C_EF_BITE0			BIT(4)
#define M16C_EF_BITE1			BIT(5)
#define M16C_EF_RCVE			BIT(6)
#define M16C_EF_TRE			BIT(7)

/* Only Leaf-based devices can report M16C error factors,
 * thus define our own error status flags for USBCANII
 */
#define USBCAN_ERROR_STATE_NONE		0
#define USBCAN_ERROR_STATE_TX_ERROR	BIT(0)
#define USBCAN_ERROR_STATE_RX_ERROR	BIT(1)
#define USBCAN_ERROR_STATE_BUSERROR	BIT(2)

/* bittiming parameters */
#define KVASER_USB_TSEG1_MIN		1
#define KVASER_USB_TSEG1_MAX		16
#define KVASER_USB_TSEG2_MIN		1
#define KVASER_USB_TSEG2_MAX		8
#define KVASER_USB_SJW_MAX		4
#define KVASER_USB_BRP_MIN		1
#define KVASER_USB_BRP_MAX		64
#define KVASER_USB_BRP_INC		1

/* ctrl modes */
#define KVASER_CTRL_MODE_NORMAL		1
#define KVASER_CTRL_MODE_SILENT		2
#define KVASER_CTRL_MODE_SELFRECEPTION	3
#define KVASER_CTRL_MODE_OFF		4

/* Extended CAN identifier flag */
#define KVASER_EXTENDED_FRAME		BIT(31)

/* Kvaser USB CAN dongles are divided into two major families:
 * - Leaf: Based on Renesas M32C, running firmware labeled as 'filo'
 * - UsbcanII: Based on Renesas M16C, running firmware labeled as 'helios'
 */
enum kvaser_usb_family {
	KVASER_LEAF,
	KVASER_USBCAN,
};

struct kvaser_msg_simple {
	u8 tid;
	u8 channel;
} __packed;

struct kvaser_msg_cardinfo {
	u8 tid;
	u8 nchannels;
	union {
		struct {
			__le32 serial_number;
			__le32 padding;
		} __packed leaf0;
		struct {
			__le32 serial_number_low;
			__le32 serial_number_high;
		} __packed usbcan0;
	} __packed;
	__le32 clock_resolution;
	__le32 mfgdate;
	u8 ean[8];
	u8 hw_revision;
	union {
		struct {
			u8 usb_hs_mode;
		} __packed leaf1;
		struct {
			u8 padding;
		} __packed usbcan1;
	} __packed;
	__le16 padding;
} __packed;

struct kvaser_msg_cardinfo2 {
	u8 tid;
	u8 reserved;
	u8 pcb_id[24];
	__le32 oem_unlock_code;
} __packed;

struct leaf_msg_softinfo {
	u8 tid;
	u8 padding0;
	__le32 sw_options;
	__le32 fw_version;
	__le16 max_outstanding_tx;
	__le16 padding1[9];
} __packed;

struct usbcan_msg_softinfo {
	u8 tid;
	u8 fw_name[5];
	__le16 max_outstanding_tx;
	u8 padding[6];
	__le32 fw_version;
	__le16 checksum;
	__le16 sw_options;
} __packed;

struct kvaser_msg_busparams {
	u8 tid;
	u8 channel;
	__le32 bitrate;
	u8 tseg1;
	u8 tseg2;
	u8 sjw;
	u8 no_samp;
} __packed;

struct kvaser_msg_tx_can {
	u8 channel;
	u8 tid;
	u8 msg[14];
	union {
		struct {
			u8 padding;
			u8 flags;
		} __packed leaf;
		struct {
			u8 flags;
			u8 padding;
		} __packed usbcan;
	} __packed;
} __packed;

struct kvaser_msg_rx_can_header {
	u8 channel;
	u8 flag;
} __packed;

struct leaf_msg_rx_can {
	u8 channel;
	u8 flag;

	__le16 time[3];
	u8 msg[14];
} __packed;

struct usbcan_msg_rx_can {
	u8 channel;
	u8 flag;

	u8 msg[14];
	__le16 time;
} __packed;

struct leaf_msg_chip_state_event {
	u8 tid;
	u8 channel;

	__le16 time[3];
	u8 tx_errors_count;
	u8 rx_errors_count;

	u8 status;
	u8 padding[3];
} __packed;

struct usbcan_msg_chip_state_event {
	u8 tid;
	u8 channel;

	u8 tx_errors_count;
	u8 rx_errors_count;
	__le16 time;

	u8 status;
	u8 padding[3];
} __packed;

struct kvaser_msg_tx_acknowledge_header {
	u8 channel;
	u8 tid;
} __packed;

struct leaf_msg_tx_acknowledge {
	u8 channel;
	u8 tid;

	__le16 time[3];
	u8 flags;
	u8 time_offset;
} __packed;

struct usbcan_msg_tx_acknowledge {
	u8 channel;
	u8 tid;

	__le16 time;
	__le16 padding;
} __packed;

struct leaf_msg_error_event {
	u8 tid;
	u8 flags;
	__le16 time[3];
	u8 channel;
	u8 padding;
	u8 tx_errors_count;
	u8 rx_errors_count;
	u8 status;
	u8 error_factor;
} __packed;

struct usbcan_msg_error_event {
	u8 tid;
	u8 padding;
	u8 tx_errors_count_ch0;
	u8 rx_errors_count_ch0;
	u8 tx_errors_count_ch1;
	u8 rx_errors_count_ch1;
	u8 status_ch0;
	u8 status_ch1;
	__le16 time;
} __packed;

struct kvaser_msg_ctrl_mode {
	u8 tid;
	u8 channel;
	u8 ctrl_mode;
	u8 padding[3];
} __packed;

struct kvaser_msg_flush_queue {
	u8 tid;
	u8 channel;
	u8 flags;
	u8 padding[3];
} __packed;

struct leaf_msg_log_message {
	u8 channel;
	u8 flags;
	__le16 time[3];
	u8 dlc;
	u8 time_offset;
	__le32 id;
	u8 data[8];
} __packed;

struct kvaser_msg {
	u8 len;
	u8 id;
	union	{
		struct kvaser_msg_simple simple;
		struct kvaser_msg_cardinfo cardinfo;
		struct kvaser_msg_cardinfo2 cardinfo2;
		struct kvaser_msg_busparams busparams;

		struct kvaser_msg_rx_can_header rx_can_header;
		struct kvaser_msg_tx_acknowledge_header tx_acknowledge_header;

		union {
			struct leaf_msg_softinfo softinfo;
			struct leaf_msg_rx_can rx_can;
			struct leaf_msg_chip_state_event chip_state_event;
			struct leaf_msg_tx_acknowledge tx_acknowledge;
			struct leaf_msg_error_event error_event;
			struct leaf_msg_log_message log_message;
		} __packed leaf;

		union {
			struct usbcan_msg_softinfo softinfo;
			struct usbcan_msg_rx_can rx_can;
			struct usbcan_msg_chip_state_event chip_state_event;
			struct usbcan_msg_tx_acknowledge tx_acknowledge;
			struct usbcan_msg_error_event error_event;
		} __packed usbcan;

		struct kvaser_msg_tx_can tx_can;
		struct kvaser_msg_ctrl_mode ctrl_mode;
		struct kvaser_msg_flush_queue flush_queue;
	} u;
} __packed;

/* Summary of a kvaser error event, for a unified Leaf/Usbcan error
 * handling. Some discrepancies between the two families exist:
 *
 * - USBCAN firmware does not report M16C "error factors"
 * - USBCAN controllers has difficulties reporting if the raised error
 *   event is for ch0 or ch1. They leave such arbitration to the OS
 *   driver by letting it compare error counters with previous values
 *   and decide the error event's channel. Thus for USBCAN, the channel
 *   field is only advisory.
 */
struct kvaser_usb_error_summary {
	u8 channel, status, txerr, rxerr;
	union {
		struct {
			u8 error_factor;
		} leaf;
		struct {
			u8 other_ch_status;
			u8 error_state;
		} usbcan;
	};
};

/* Context for an outstanding, not yet ACKed, transmission */
struct kvaser_usb_tx_urb_context {
	struct kvaser_usb_net_priv *priv;
	u32 echo_index;
	int dlc;
};

struct kvaser_usb {
	struct usb_device *udev;
	struct kvaser_usb_net_priv *nets[MAX_NET_DEVICES];

	struct usb_endpoint_descriptor *bulk_in, *bulk_out;
	struct usb_anchor rx_submitted;

	/* @max_tx_urbs: Firmware-reported maximum number of outstanding,
	 * not yet ACKed, transmissions on this device. This value is
	 * also used as a sentinel for marking free tx contexts.
	 */
	u32 fw_version;
	unsigned int nchannels;
	unsigned int max_tx_urbs;
	enum kvaser_usb_family family;

	bool rxinitdone;
	void *rxbuf[MAX_RX_URBS];
	dma_addr_t rxbuf_dma[MAX_RX_URBS];
};

struct kvaser_usb_net_priv {
	struct can_priv can;
	struct can_berr_counter bec;

	struct kvaser_usb *dev;
	struct net_device *netdev;
	int channel;

	struct completion start_comp, stop_comp;
	struct usb_anchor tx_submitted;

	spinlock_t tx_contexts_lock;
	int active_tx_contexts;
	struct kvaser_usb_tx_urb_context tx_contexts[];
};

static const struct usb_device_id kvaser_usb_table[] = {
	/* Leaf family IDs */
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_DEVEL_PRODUCT_ID) },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_PRODUCT_ID) },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_SPRO_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_LS_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_SWC_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_LIN_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_SPRO_LS_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_SPRO_SWC_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO2_DEVEL_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO2_HSHS_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_UPRO_HSHS_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_GI_PRODUCT_ID) },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_OBDII_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS |
			       KVASER_HAS_SILENT_MODE },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO2_HSLS_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_CH_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_BLACKBIRD_SPRO_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_OEM_MERCURY_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_OEM_LEAF_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_CAN_R_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_V2_PRODUCT_ID) },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MINI_PCIE_HS_PRODUCT_ID) },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LIGHT_HS_V2_OEM_PRODUCT_ID) },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_LIGHT_2HS_PRODUCT_ID) },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MINI_PCIE_2HS_PRODUCT_ID) },

	/* USBCANII family IDs */
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN2_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_REVB_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_MEMORATOR_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },
	{ USB_DEVICE(KVASER_VENDOR_ID, USB_VCI2_PRODUCT_ID),
		.driver_info = KVASER_HAS_TXRX_ERRORS },

	{ }
};
MODULE_DEVICE_TABLE(usb, kvaser_usb_table);

static inline int kvaser_usb_send_msg(const struct kvaser_usb *dev,
				      struct kvaser_msg *msg)
{
	int actual_len;

	return usb_bulk_msg(dev->udev,
			    usb_sndbulkpipe(dev->udev,
					dev->bulk_out->bEndpointAddress),
			    msg, msg->len, &actual_len,
			    USB_SEND_TIMEOUT);
}

static int kvaser_usb_wait_msg(const struct kvaser_usb *dev, u8 id,
			       struct kvaser_msg *msg)
{
	struct kvaser_msg *tmp;
	void *buf;
	int actual_len;
	int err;
	int pos;
	unsigned long to = jiffies + msecs_to_jiffies(USB_RECV_TIMEOUT);

	buf = kzalloc(RX_BUFFER_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	do {
		err = usb_bulk_msg(dev->udev,
				   usb_rcvbulkpipe(dev->udev,
					dev->bulk_in->bEndpointAddress),
				   buf, RX_BUFFER_SIZE, &actual_len,
				   USB_RECV_TIMEOUT);
		if (err < 0)
			goto end;

		pos = 0;
		while (pos <= actual_len - MSG_HEADER_LEN) {
			tmp = buf + pos;

			/* Handle messages crossing the USB endpoint max packet
			 * size boundary. Check kvaser_usb_read_bulk_callback()
			 * for further details.
			 */
			if (tmp->len == 0) {
				pos = round_up(pos, le16_to_cpu(dev->bulk_in->
								wMaxPacketSize));
				continue;
			}

			if (pos + tmp->len > actual_len) {
				dev_err_ratelimited(dev->udev->dev.parent,
						    "Format error\n");
				break;
			}

			if (tmp->id == id) {
				memcpy(msg, tmp, tmp->len);
				goto end;
			}

			pos += tmp->len;
		}
	} while (time_before(jiffies, to));

	err = -EINVAL;

end:
	kfree(buf);

	return err;
}

static int kvaser_usb_send_simple_msg(const struct kvaser_usb *dev,
				      u8 msg_id, int channel)
{
	struct kvaser_msg *msg;
	int rc;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->id = msg_id;
	msg->len = MSG_HEADER_LEN + sizeof(struct kvaser_msg_simple);
	msg->u.simple.channel = channel;
	msg->u.simple.tid = 0xff;

	rc = kvaser_usb_send_msg(dev, msg);

	kfree(msg);
	return rc;
}

static int kvaser_usb_get_software_info(struct kvaser_usb *dev)
{
	struct kvaser_msg msg;
	int err;

	err = kvaser_usb_send_simple_msg(dev, CMD_GET_SOFTWARE_INFO, 0);
	if (err)
		return err;

	err = kvaser_usb_wait_msg(dev, CMD_GET_SOFTWARE_INFO_REPLY, &msg);
	if (err)
		return err;

	switch (dev->family) {
	case KVASER_LEAF:
		dev->fw_version = le32_to_cpu(msg.u.leaf.softinfo.fw_version);
		dev->max_tx_urbs =
			le16_to_cpu(msg.u.leaf.softinfo.max_outstanding_tx);
		break;
	case KVASER_USBCAN:
		dev->fw_version = le32_to_cpu(msg.u.usbcan.softinfo.fw_version);
		dev->max_tx_urbs =
			le16_to_cpu(msg.u.usbcan.softinfo.max_outstanding_tx);
		break;
	}

	return 0;
}

static int kvaser_usb_get_card_info(struct kvaser_usb *dev)
{
	struct kvaser_msg msg;
	int err;

	err = kvaser_usb_send_simple_msg(dev, CMD_GET_CARD_INFO, 0);
	if (err)
		return err;

	err = kvaser_usb_wait_msg(dev, CMD_GET_CARD_INFO_REPLY, &msg);
	if (err)
		return err;

	dev->nchannels = msg.u.cardinfo.nchannels;
	if ((dev->nchannels > MAX_NET_DEVICES) ||
	    (dev->family == KVASER_USBCAN &&
	     dev->nchannels > MAX_USBCAN_NET_DEVICES))
		return -EINVAL;

	return 0;
}

static void kvaser_usb_tx_acknowledge(const struct kvaser_usb *dev,
				      const struct kvaser_msg *msg)
{
	struct net_device_stats *stats;
	struct kvaser_usb_tx_urb_context *context;
	struct kvaser_usb_net_priv *priv;
	struct sk_buff *skb;
	struct can_frame *cf;
	unsigned long flags;
	u8 channel, tid;

	channel = msg->u.tx_acknowledge_header.channel;
	tid = msg->u.tx_acknowledge_header.tid;

	if (channel >= dev->nchannels) {
		dev_err(dev->udev->dev.parent,
			"Invalid channel number (%d)\n", channel);
		return;
	}

	priv = dev->nets[channel];

	if (!netif_device_present(priv->netdev))
		return;

	stats = &priv->netdev->stats;

	context = &priv->tx_contexts[tid % dev->max_tx_urbs];

	/* Sometimes the state change doesn't come after a bus-off event */
	if (priv->can.restart_ms &&
	    (priv->can.state >= CAN_STATE_BUS_OFF)) {
		skb = alloc_can_err_skb(priv->netdev, &cf);
		if (skb) {
			cf->can_id |= CAN_ERR_RESTARTED;

			stats->rx_packets++;
			stats->rx_bytes += cf->can_dlc;
			netif_rx(skb);
		} else {
			netdev_err(priv->netdev,
				   "No memory left for err_skb\n");
		}

		priv->can.can_stats.restarts++;
		netif_carrier_on(priv->netdev);

		priv->can.state = CAN_STATE_ERROR_ACTIVE;
	}

	stats->tx_packets++;
	stats->tx_bytes += context->dlc;

	spin_lock_irqsave(&priv->tx_contexts_lock, flags);

	can_get_echo_skb(priv->netdev, context->echo_index);
	context->echo_index = dev->max_tx_urbs;
	--priv->active_tx_contexts;
	netif_wake_queue(priv->netdev);

	spin_unlock_irqrestore(&priv->tx_contexts_lock, flags);
}

static void kvaser_usb_simple_msg_callback(struct urb *urb)
{
	struct net_device *netdev = urb->context;

	kfree(urb->transfer_buffer);

	if (urb->status)
		netdev_warn(netdev, "urb status received: %d\n",
			    urb->status);
}

static int kvaser_usb_simple_msg_async(struct kvaser_usb_net_priv *priv,
				       u8 msg_id)
{
	struct kvaser_usb *dev = priv->dev;
	struct net_device *netdev = priv->netdev;
	struct kvaser_msg *msg;
	struct urb *urb;
	void *buf;
	int err;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(sizeof(struct kvaser_msg), GFP_ATOMIC);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	msg = (struct kvaser_msg *)buf;
	msg->len = MSG_HEADER_LEN + sizeof(struct kvaser_msg_simple);
	msg->id = msg_id;
	msg->u.simple.channel = priv->channel;

	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev,
					  dev->bulk_out->bEndpointAddress),
			  buf, msg->len,
			  kvaser_usb_simple_msg_callback, netdev);
	usb_anchor_urb(urb, &priv->tx_submitted);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		netdev_err(netdev, "Error transmitting URB\n");
		usb_unanchor_urb(urb);
		kfree(buf);
		usb_free_urb(urb);
		return err;
	}

	usb_free_urb(urb);

	return 0;
}

static void kvaser_usb_rx_error_update_can_state(struct kvaser_usb_net_priv *priv,
						 const struct kvaser_usb_error_summary *es,
						 struct can_frame *cf)
{
	struct kvaser_usb *dev = priv->dev;
	struct net_device_stats *stats = &priv->netdev->stats;
	enum can_state cur_state, new_state, tx_state, rx_state;

	netdev_dbg(priv->netdev, "Error status: 0x%02x\n", es->status);

	new_state = cur_state = priv->can.state;

	if (es->status & (M16C_STATE_BUS_OFF | M16C_STATE_BUS_RESET))
		new_state = CAN_STATE_BUS_OFF;
	else if (es->status & M16C_STATE_BUS_PASSIVE)
		new_state = CAN_STATE_ERROR_PASSIVE;
	else if (es->status & M16C_STATE_BUS_ERROR) {
		/* Guard against spurious error events after a busoff */
		if (cur_state < CAN_STATE_BUS_OFF) {
			if ((es->txerr >= 128) || (es->rxerr >= 128))
				new_state = CAN_STATE_ERROR_PASSIVE;
			else if ((es->txerr >= 96) || (es->rxerr >= 96))
				new_state = CAN_STATE_ERROR_WARNING;
			else if (cur_state > CAN_STATE_ERROR_ACTIVE)
				new_state = CAN_STATE_ERROR_ACTIVE;
		}
	}

	if (!es->status)
		new_state = CAN_STATE_ERROR_ACTIVE;

	if (new_state != cur_state) {
		tx_state = (es->txerr >= es->rxerr) ? new_state : 0;
		rx_state = (es->txerr <= es->rxerr) ? new_state : 0;

		can_change_state(priv->netdev, cf, tx_state, rx_state);
	}

	if (priv->can.restart_ms &&
	    (cur_state >= CAN_STATE_BUS_OFF) &&
	    (new_state < CAN_STATE_BUS_OFF)) {
		priv->can.can_stats.restarts++;
	}

	switch (dev->family) {
	case KVASER_LEAF:
		if (es->leaf.error_factor) {
			priv->can.can_stats.bus_error++;
			stats->rx_errors++;
		}
		break;
	case KVASER_USBCAN:
		if (es->usbcan.error_state & USBCAN_ERROR_STATE_TX_ERROR)
			stats->tx_errors++;
		if (es->usbcan.error_state & USBCAN_ERROR_STATE_RX_ERROR)
			stats->rx_errors++;
		if (es->usbcan.error_state & USBCAN_ERROR_STATE_BUSERROR) {
			priv->can.can_stats.bus_error++;
		}
		break;
	}

	priv->bec.txerr = es->txerr;
	priv->bec.rxerr = es->rxerr;
}

static void kvaser_usb_rx_error(const struct kvaser_usb *dev,
				const struct kvaser_usb_error_summary *es)
{
	struct can_frame *cf, tmp_cf = { .can_id = CAN_ERR_FLAG, .can_dlc = CAN_ERR_DLC };
	struct sk_buff *skb;
	struct net_device_stats *stats;
	struct kvaser_usb_net_priv *priv;
	enum can_state old_state, new_state;

	if (es->channel >= dev->nchannels) {
		dev_err(dev->udev->dev.parent,
			"Invalid channel number (%d)\n", es->channel);
		return;
	}

	priv = dev->nets[es->channel];
	stats = &priv->netdev->stats;

	/* Update all of the can interface's state and error counters before
	 * trying any memory allocation that can actually fail with -ENOMEM.
	 *
	 * We send a temporary stack-allocated error can frame to
	 * can_change_state() for the very same reason.
	 *
	 * TODO: Split can_change_state() responsibility between updating the
	 * can interface's state and counters, and the setting up of can error
	 * frame ID and data to userspace. Remove stack allocation afterwards.
	 */
	old_state = priv->can.state;
	kvaser_usb_rx_error_update_can_state(priv, es, &tmp_cf);
	new_state = priv->can.state;

	skb = alloc_can_err_skb(priv->netdev, &cf);
	if (!skb) {
		stats->rx_dropped++;
		return;
	}
	memcpy(cf, &tmp_cf, sizeof(*cf));

	if (new_state != old_state) {
		if (es->status &
		    (M16C_STATE_BUS_OFF | M16C_STATE_BUS_RESET)) {
			if (!priv->can.restart_ms)
				kvaser_usb_simple_msg_async(priv, CMD_STOP_CHIP);
			netif_carrier_off(priv->netdev);
		}

		if (priv->can.restart_ms &&
		    (old_state >= CAN_STATE_BUS_OFF) &&
		    (new_state < CAN_STATE_BUS_OFF)) {
			cf->can_id |= CAN_ERR_RESTARTED;
			netif_carrier_on(priv->netdev);
		}
	}

	switch (dev->family) {
	case KVASER_LEAF:
		if (es->leaf.error_factor) {
			cf->can_id |= CAN_ERR_BUSERROR | CAN_ERR_PROT;

			if (es->leaf.error_factor & M16C_EF_ACKE)
				cf->data[3] = CAN_ERR_PROT_LOC_ACK;
			if (es->leaf.error_factor & M16C_EF_CRCE)
				cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
			if (es->leaf.error_factor & M16C_EF_FORME)
				cf->data[2] |= CAN_ERR_PROT_FORM;
			if (es->leaf.error_factor & M16C_EF_STFE)
				cf->data[2] |= CAN_ERR_PROT_STUFF;
			if (es->leaf.error_factor & M16C_EF_BITE0)
				cf->data[2] |= CAN_ERR_PROT_BIT0;
			if (es->leaf.error_factor & M16C_EF_BITE1)
				cf->data[2] |= CAN_ERR_PROT_BIT1;
			if (es->leaf.error_factor & M16C_EF_TRE)
				cf->data[2] |= CAN_ERR_PROT_TX;
		}
		break;
	case KVASER_USBCAN:
		if (es->usbcan.error_state & USBCAN_ERROR_STATE_BUSERROR) {
			cf->can_id |= CAN_ERR_BUSERROR;
		}
		break;
	}

	cf->data[6] = es->txerr;
	cf->data[7] = es->rxerr;

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);
}

/* For USBCAN, report error to userspace iff the channels's errors counter
 * has changed, or we're the only channel seeing a bus error state.
 */
static void kvaser_usbcan_conditionally_rx_error(const struct kvaser_usb *dev,
						 struct kvaser_usb_error_summary *es)
{
	struct kvaser_usb_net_priv *priv;
	int channel;
	bool report_error;

	channel = es->channel;
	if (channel >= dev->nchannels) {
		dev_err(dev->udev->dev.parent,
			"Invalid channel number (%d)\n", channel);
		return;
	}

	priv = dev->nets[channel];
	report_error = false;

	if (es->txerr != priv->bec.txerr) {
		es->usbcan.error_state |= USBCAN_ERROR_STATE_TX_ERROR;
		report_error = true;
	}
	if (es->rxerr != priv->bec.rxerr) {
		es->usbcan.error_state |= USBCAN_ERROR_STATE_RX_ERROR;
		report_error = true;
	}
	if ((es->status & M16C_STATE_BUS_ERROR) &&
	    !(es->usbcan.other_ch_status & M16C_STATE_BUS_ERROR)) {
		es->usbcan.error_state |= USBCAN_ERROR_STATE_BUSERROR;
		report_error = true;
	}

	if (report_error)
		kvaser_usb_rx_error(dev, es);
}

static void kvaser_usbcan_rx_error(const struct kvaser_usb *dev,
				   const struct kvaser_msg *msg)
{
	struct kvaser_usb_error_summary es = { };

	switch (msg->id) {
	/* Sometimes errors are sent as unsolicited chip state events */
	case CMD_CHIP_STATE_EVENT:
		es.channel = msg->u.usbcan.chip_state_event.channel;
		es.status =  msg->u.usbcan.chip_state_event.status;
		es.txerr = msg->u.usbcan.chip_state_event.tx_errors_count;
		es.rxerr = msg->u.usbcan.chip_state_event.rx_errors_count;
		kvaser_usbcan_conditionally_rx_error(dev, &es);
		break;

	case CMD_CAN_ERROR_EVENT:
		es.channel = 0;
		es.status = msg->u.usbcan.error_event.status_ch0;
		es.txerr = msg->u.usbcan.error_event.tx_errors_count_ch0;
		es.rxerr = msg->u.usbcan.error_event.rx_errors_count_ch0;
		es.usbcan.other_ch_status =
			msg->u.usbcan.error_event.status_ch1;
		kvaser_usbcan_conditionally_rx_error(dev, &es);

		/* The USBCAN firmware supports up to 2 channels.
		 * Now that ch0 was checked, check if ch1 has any errors.
		 */
		if (dev->nchannels == MAX_USBCAN_NET_DEVICES) {
			es.channel = 1;
			es.status = msg->u.usbcan.error_event.status_ch1;
			es.txerr = msg->u.usbcan.error_event.tx_errors_count_ch1;
			es.rxerr = msg->u.usbcan.error_event.rx_errors_count_ch1;
			es.usbcan.other_ch_status =
				msg->u.usbcan.error_event.status_ch0;
			kvaser_usbcan_conditionally_rx_error(dev, &es);
		}
		break;

	default:
		dev_err(dev->udev->dev.parent, "Invalid msg id (%d)\n",
			msg->id);
	}
}

static void kvaser_leaf_rx_error(const struct kvaser_usb *dev,
				 const struct kvaser_msg *msg)
{
	struct kvaser_usb_error_summary es = { };

	switch (msg->id) {
	case CMD_CAN_ERROR_EVENT:
		es.channel = msg->u.leaf.error_event.channel;
		es.status =  msg->u.leaf.error_event.status;
		es.txerr = msg->u.leaf.error_event.tx_errors_count;
		es.rxerr = msg->u.leaf.error_event.rx_errors_count;
		es.leaf.error_factor = msg->u.leaf.error_event.error_factor;
		break;
	case CMD_LEAF_LOG_MESSAGE:
		es.channel = msg->u.leaf.log_message.channel;
		es.status = msg->u.leaf.log_message.data[0];
		es.txerr = msg->u.leaf.log_message.data[2];
		es.rxerr = msg->u.leaf.log_message.data[3];
		es.leaf.error_factor = msg->u.leaf.log_message.data[1];
		break;
	case CMD_CHIP_STATE_EVENT:
		es.channel = msg->u.leaf.chip_state_event.channel;
		es.status =  msg->u.leaf.chip_state_event.status;
		es.txerr = msg->u.leaf.chip_state_event.tx_errors_count;
		es.rxerr = msg->u.leaf.chip_state_event.rx_errors_count;
		es.leaf.error_factor = 0;
		break;
	default:
		dev_err(dev->udev->dev.parent, "Invalid msg id (%d)\n",
			msg->id);
		return;
	}

	kvaser_usb_rx_error(dev, &es);
}

static void kvaser_usb_rx_can_err(const struct kvaser_usb_net_priv *priv,
				  const struct kvaser_msg *msg)
{
	struct can_frame *cf;
	struct sk_buff *skb;
	struct net_device_stats *stats = &priv->netdev->stats;

	if (msg->u.rx_can_header.flag & (MSG_FLAG_ERROR_FRAME |
					 MSG_FLAG_NERR)) {
		netdev_err(priv->netdev, "Unknown error (flags: 0x%02x)\n",
			   msg->u.rx_can_header.flag);

		stats->rx_errors++;
		return;
	}

	if (msg->u.rx_can_header.flag & MSG_FLAG_OVERRUN) {
		stats->rx_over_errors++;
		stats->rx_errors++;

		skb = alloc_can_err_skb(priv->netdev, &cf);
		if (!skb) {
			stats->rx_dropped++;
			return;
		}

		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}
}

static void kvaser_usb_rx_can_msg(const struct kvaser_usb *dev,
				  const struct kvaser_msg *msg)
{
	struct kvaser_usb_net_priv *priv;
	struct can_frame *cf;
	struct sk_buff *skb;
	struct net_device_stats *stats;
	u8 channel = msg->u.rx_can_header.channel;
	const u8 *rx_msg = NULL;	/* GCC */

	if (channel >= dev->nchannels) {
		dev_err(dev->udev->dev.parent,
			"Invalid channel number (%d)\n", channel);
		return;
	}

	priv = dev->nets[channel];
	stats = &priv->netdev->stats;

	if ((msg->u.rx_can_header.flag & MSG_FLAG_ERROR_FRAME) &&
	    (dev->family == KVASER_LEAF && msg->id == CMD_LEAF_LOG_MESSAGE)) {
		kvaser_leaf_rx_error(dev, msg);
		return;
	} else if (msg->u.rx_can_header.flag & (MSG_FLAG_ERROR_FRAME |
						MSG_FLAG_NERR |
						MSG_FLAG_OVERRUN)) {
		kvaser_usb_rx_can_err(priv, msg);
		return;
	} else if (msg->u.rx_can_header.flag & ~MSG_FLAG_REMOTE_FRAME) {
		netdev_warn(priv->netdev,
			    "Unhandled frame (flags: 0x%02x)",
			    msg->u.rx_can_header.flag);
		return;
	}

	switch (dev->family) {
	case KVASER_LEAF:
		rx_msg = msg->u.leaf.rx_can.msg;
		break;
	case KVASER_USBCAN:
		rx_msg = msg->u.usbcan.rx_can.msg;
		break;
	}

	skb = alloc_can_skb(priv->netdev, &cf);
	if (!skb) {
		stats->tx_dropped++;
		return;
	}

	if (dev->family == KVASER_LEAF && msg->id == CMD_LEAF_LOG_MESSAGE) {
		cf->can_id = le32_to_cpu(msg->u.leaf.log_message.id);
		if (cf->can_id & KVASER_EXTENDED_FRAME)
			cf->can_id &= CAN_EFF_MASK | CAN_EFF_FLAG;
		else
			cf->can_id &= CAN_SFF_MASK;

		cf->can_dlc = get_can_dlc(msg->u.leaf.log_message.dlc);

		if (msg->u.leaf.log_message.flags & MSG_FLAG_REMOTE_FRAME)
			cf->can_id |= CAN_RTR_FLAG;
		else
			memcpy(cf->data, &msg->u.leaf.log_message.data,
			       cf->can_dlc);
	} else {
		cf->can_id = ((rx_msg[0] & 0x1f) << 6) | (rx_msg[1] & 0x3f);

		if (msg->id == CMD_RX_EXT_MESSAGE) {
			cf->can_id <<= 18;
			cf->can_id |= ((rx_msg[2] & 0x0f) << 14) |
				      ((rx_msg[3] & 0xff) << 6) |
				      (rx_msg[4] & 0x3f);
			cf->can_id |= CAN_EFF_FLAG;
		}

		cf->can_dlc = get_can_dlc(rx_msg[5]);

		if (msg->u.rx_can_header.flag & MSG_FLAG_REMOTE_FRAME)
			cf->can_id |= CAN_RTR_FLAG;
		else
			memcpy(cf->data, &rx_msg[6],
			       cf->can_dlc);
	}

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);
}

static void kvaser_usb_start_chip_reply(const struct kvaser_usb *dev,
					const struct kvaser_msg *msg)
{
	struct kvaser_usb_net_priv *priv;
	u8 channel = msg->u.simple.channel;

	if (channel >= dev->nchannels) {
		dev_err(dev->udev->dev.parent,
			"Invalid channel number (%d)\n", channel);
		return;
	}

	priv = dev->nets[channel];

	if (completion_done(&priv->start_comp) &&
	    netif_queue_stopped(priv->netdev)) {
		netif_wake_queue(priv->netdev);
	} else {
		netif_start_queue(priv->netdev);
		complete(&priv->start_comp);
	}
}

static void kvaser_usb_stop_chip_reply(const struct kvaser_usb *dev,
				       const struct kvaser_msg *msg)
{
	struct kvaser_usb_net_priv *priv;
	u8 channel = msg->u.simple.channel;

	if (channel >= dev->nchannels) {
		dev_err(dev->udev->dev.parent,
			"Invalid channel number (%d)\n", channel);
		return;
	}

	priv = dev->nets[channel];

	complete(&priv->stop_comp);
}

static void kvaser_usb_handle_message(const struct kvaser_usb *dev,
				      const struct kvaser_msg *msg)
{
	switch (msg->id) {
	case CMD_START_CHIP_REPLY:
		kvaser_usb_start_chip_reply(dev, msg);
		break;

	case CMD_STOP_CHIP_REPLY:
		kvaser_usb_stop_chip_reply(dev, msg);
		break;

	case CMD_RX_STD_MESSAGE:
	case CMD_RX_EXT_MESSAGE:
		kvaser_usb_rx_can_msg(dev, msg);
		break;

	case CMD_LEAF_LOG_MESSAGE:
		if (dev->family != KVASER_LEAF)
			goto warn;
		kvaser_usb_rx_can_msg(dev, msg);
		break;

	case CMD_CHIP_STATE_EVENT:
	case CMD_CAN_ERROR_EVENT:
		if (dev->family == KVASER_LEAF)
			kvaser_leaf_rx_error(dev, msg);
		else
			kvaser_usbcan_rx_error(dev, msg);
		break;

	case CMD_TX_ACKNOWLEDGE:
		kvaser_usb_tx_acknowledge(dev, msg);
		break;

	/* Ignored messages */
	case CMD_USBCAN_CLOCK_OVERFLOW_EVENT:
		if (dev->family != KVASER_USBCAN)
			goto warn;
		break;

	case CMD_FLUSH_QUEUE_REPLY:
		if (dev->family != KVASER_LEAF)
			goto warn;
		break;

	default:
warn:		dev_warn(dev->udev->dev.parent,
			 "Unhandled message (%d)\n", msg->id);
		break;
	}
}

static void kvaser_usb_read_bulk_callback(struct urb *urb)
{
	struct kvaser_usb *dev = urb->context;
	struct kvaser_msg *msg;
	int pos = 0;
	int err, i;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -EPIPE:
	case -EPROTO:
	case -ESHUTDOWN:
		return;
	default:
		dev_info(dev->udev->dev.parent, "Rx URB aborted (%d)\n",
			 urb->status);
		goto resubmit_urb;
	}

	while (pos <= (int)(urb->actual_length - MSG_HEADER_LEN)) {
		msg = urb->transfer_buffer + pos;

		/* The Kvaser firmware can only read and write messages that
		 * does not cross the USB's endpoint wMaxPacketSize boundary.
		 * If a follow-up command crosses such boundary, firmware puts
		 * a placeholder zero-length command in its place then aligns
		 * the real command to the next max packet size.
		 *
		 * Handle such cases or we're going to miss a significant
		 * number of events in case of a heavy rx load on the bus.
		 */
		if (msg->len == 0) {
			pos = round_up(pos, le16_to_cpu(dev->bulk_in->
							wMaxPacketSize));
			continue;
		}

		if (pos + msg->len > urb->actual_length) {
			dev_err_ratelimited(dev->udev->dev.parent,
					    "Format error\n");
			break;
		}

		kvaser_usb_handle_message(dev, msg);
		pos += msg->len;
	}

resubmit_urb:
	usb_fill_bulk_urb(urb, dev->udev,
			  usb_rcvbulkpipe(dev->udev,
					  dev->bulk_in->bEndpointAddress),
			  urb->transfer_buffer, RX_BUFFER_SIZE,
			  kvaser_usb_read_bulk_callback, dev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err == -ENODEV) {
		for (i = 0; i < dev->nchannels; i++) {
			if (!dev->nets[i])
				continue;

			netif_device_detach(dev->nets[i]->netdev);
		}
	} else if (err) {
		dev_err(dev->udev->dev.parent,
			"Failed resubmitting read bulk urb: %d\n", err);
	}

	return;
}

static int kvaser_usb_setup_rx_urbs(struct kvaser_usb *dev)
{
	int i, err = 0;

	if (dev->rxinitdone)
		return 0;

	for (i = 0; i < MAX_RX_URBS; i++) {
		struct urb *urb = NULL;
		u8 *buf = NULL;
		dma_addr_t buf_dma;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			err = -ENOMEM;
			break;
		}

		buf = usb_alloc_coherent(dev->udev, RX_BUFFER_SIZE,
					 GFP_KERNEL, &buf_dma);
		if (!buf) {
			dev_warn(dev->udev->dev.parent,
				 "No memory left for USB buffer\n");
			usb_free_urb(urb);
			err = -ENOMEM;
			break;
		}

		usb_fill_bulk_urb(urb, dev->udev,
				  usb_rcvbulkpipe(dev->udev,
					  dev->bulk_in->bEndpointAddress),
				  buf, RX_BUFFER_SIZE,
				  kvaser_usb_read_bulk_callback,
				  dev);
		urb->transfer_dma = buf_dma;
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		usb_anchor_urb(urb, &dev->rx_submitted);

		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			usb_unanchor_urb(urb);
			usb_free_coherent(dev->udev, RX_BUFFER_SIZE, buf,
					  buf_dma);
			usb_free_urb(urb);
			break;
		}

		dev->rxbuf[i] = buf;
		dev->rxbuf_dma[i] = buf_dma;

		usb_free_urb(urb);
	}

	if (i == 0) {
		dev_warn(dev->udev->dev.parent,
			 "Cannot setup read URBs, error %d\n", err);
		return err;
	} else if (i < MAX_RX_URBS) {
		dev_warn(dev->udev->dev.parent,
			 "RX performances may be slow\n");
	}

	dev->rxinitdone = true;

	return 0;
}

static int kvaser_usb_set_opt_mode(const struct kvaser_usb_net_priv *priv)
{
	struct kvaser_msg *msg;
	int rc;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->id = CMD_SET_CTRL_MODE;
	msg->len = MSG_HEADER_LEN + sizeof(struct kvaser_msg_ctrl_mode);
	msg->u.ctrl_mode.tid = 0xff;
	msg->u.ctrl_mode.channel = priv->channel;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		msg->u.ctrl_mode.ctrl_mode = KVASER_CTRL_MODE_SILENT;
	else
		msg->u.ctrl_mode.ctrl_mode = KVASER_CTRL_MODE_NORMAL;

	rc = kvaser_usb_send_msg(priv->dev, msg);

	kfree(msg);
	return rc;
}

static int kvaser_usb_start_chip(struct kvaser_usb_net_priv *priv)
{
	int err;

	init_completion(&priv->start_comp);

	err = kvaser_usb_send_simple_msg(priv->dev, CMD_START_CHIP,
					 priv->channel);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&priv->start_comp,
					 msecs_to_jiffies(START_TIMEOUT)))
		return -ETIMEDOUT;

	return 0;
}

static int kvaser_usb_open(struct net_device *netdev)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct kvaser_usb *dev = priv->dev;
	int err;

	err = open_candev(netdev);
	if (err)
		return err;

	err = kvaser_usb_setup_rx_urbs(dev);
	if (err)
		goto error;

	err = kvaser_usb_set_opt_mode(priv);
	if (err)
		goto error;

	err = kvaser_usb_start_chip(priv);
	if (err) {
		netdev_warn(netdev, "Cannot start device, error %d\n", err);
		goto error;
	}

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;

error:
	close_candev(netdev);
	return err;
}

static void kvaser_usb_reset_tx_urb_contexts(struct kvaser_usb_net_priv *priv)
{
	int i, max_tx_urbs;

	max_tx_urbs = priv->dev->max_tx_urbs;

	priv->active_tx_contexts = 0;
	for (i = 0; i < max_tx_urbs; i++)
		priv->tx_contexts[i].echo_index = max_tx_urbs;
}

/* This method might sleep. Do not call it in the atomic context
 * of URB completions.
 */
static void kvaser_usb_unlink_tx_urbs(struct kvaser_usb_net_priv *priv)
{
	usb_kill_anchored_urbs(&priv->tx_submitted);
	kvaser_usb_reset_tx_urb_contexts(priv);
}

static void kvaser_usb_unlink_all_urbs(struct kvaser_usb *dev)
{
	int i;

	usb_kill_anchored_urbs(&dev->rx_submitted);

	for (i = 0; i < MAX_RX_URBS; i++)
		usb_free_coherent(dev->udev, RX_BUFFER_SIZE,
				  dev->rxbuf[i],
				  dev->rxbuf_dma[i]);

	for (i = 0; i < dev->nchannels; i++) {
		struct kvaser_usb_net_priv *priv = dev->nets[i];

		if (priv)
			kvaser_usb_unlink_tx_urbs(priv);
	}
}

static int kvaser_usb_stop_chip(struct kvaser_usb_net_priv *priv)
{
	int err;

	init_completion(&priv->stop_comp);

	err = kvaser_usb_send_simple_msg(priv->dev, CMD_STOP_CHIP,
					 priv->channel);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&priv->stop_comp,
					 msecs_to_jiffies(STOP_TIMEOUT)))
		return -ETIMEDOUT;

	return 0;
}

static int kvaser_usb_flush_queue(struct kvaser_usb_net_priv *priv)
{
	struct kvaser_msg *msg;
	int rc;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->id = CMD_FLUSH_QUEUE;
	msg->len = MSG_HEADER_LEN + sizeof(struct kvaser_msg_flush_queue);
	msg->u.flush_queue.channel = priv->channel;
	msg->u.flush_queue.flags = 0x00;

	rc = kvaser_usb_send_msg(priv->dev, msg);

	kfree(msg);
	return rc;
}

static int kvaser_usb_close(struct net_device *netdev)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct kvaser_usb *dev = priv->dev;
	int err;

	netif_stop_queue(netdev);

	err = kvaser_usb_flush_queue(priv);
	if (err)
		netdev_warn(netdev, "Cannot flush queue, error %d\n", err);

	err = kvaser_usb_send_simple_msg(dev, CMD_RESET_CHIP, priv->channel);
	if (err)
		netdev_warn(netdev, "Cannot reset card, error %d\n", err);

	err = kvaser_usb_stop_chip(priv);
	if (err)
		netdev_warn(netdev, "Cannot stop device, error %d\n", err);

	/* reset tx contexts */
	kvaser_usb_unlink_tx_urbs(priv);

	priv->can.state = CAN_STATE_STOPPED;
	close_candev(priv->netdev);

	return 0;
}

static void kvaser_usb_write_bulk_callback(struct urb *urb)
{
	struct kvaser_usb_tx_urb_context *context = urb->context;
	struct kvaser_usb_net_priv *priv;
	struct net_device *netdev;

	if (WARN_ON(!context))
		return;

	priv = context->priv;
	netdev = priv->netdev;

	kfree(urb->transfer_buffer);

	if (!netif_device_present(netdev))
		return;

	if (urb->status)
		netdev_info(netdev, "Tx URB aborted (%d)\n", urb->status);
}

static netdev_tx_t kvaser_usb_start_xmit(struct sk_buff *skb,
					 struct net_device *netdev)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct kvaser_usb *dev = priv->dev;
	struct net_device_stats *stats = &netdev->stats;
	struct can_frame *cf = (struct can_frame *)skb->data;
	struct kvaser_usb_tx_urb_context *context = NULL;
	struct urb *urb;
	void *buf;
	struct kvaser_msg *msg;
	int i, err, ret = NETDEV_TX_OK;
	u8 *msg_tx_can_flags = NULL;		/* GCC */
	unsigned long flags;

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		stats->tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	buf = kmalloc(sizeof(struct kvaser_msg), GFP_ATOMIC);
	if (!buf) {
		stats->tx_dropped++;
		dev_kfree_skb(skb);
		goto freeurb;
	}

	msg = buf;
	msg->len = MSG_HEADER_LEN + sizeof(struct kvaser_msg_tx_can);
	msg->u.tx_can.channel = priv->channel;

	switch (dev->family) {
	case KVASER_LEAF:
		msg_tx_can_flags = &msg->u.tx_can.leaf.flags;
		break;
	case KVASER_USBCAN:
		msg_tx_can_flags = &msg->u.tx_can.usbcan.flags;
		break;
	}

	*msg_tx_can_flags = 0;

	if (cf->can_id & CAN_EFF_FLAG) {
		msg->id = CMD_TX_EXT_MESSAGE;
		msg->u.tx_can.msg[0] = (cf->can_id >> 24) & 0x1f;
		msg->u.tx_can.msg[1] = (cf->can_id >> 18) & 0x3f;
		msg->u.tx_can.msg[2] = (cf->can_id >> 14) & 0x0f;
		msg->u.tx_can.msg[3] = (cf->can_id >> 6) & 0xff;
		msg->u.tx_can.msg[4] = cf->can_id & 0x3f;
	} else {
		msg->id = CMD_TX_STD_MESSAGE;
		msg->u.tx_can.msg[0] = (cf->can_id >> 6) & 0x1f;
		msg->u.tx_can.msg[1] = cf->can_id & 0x3f;
	}

	msg->u.tx_can.msg[5] = cf->can_dlc;
	memcpy(&msg->u.tx_can.msg[6], cf->data, cf->can_dlc);

	if (cf->can_id & CAN_RTR_FLAG)
		*msg_tx_can_flags |= MSG_FLAG_REMOTE_FRAME;

	spin_lock_irqsave(&priv->tx_contexts_lock, flags);
	for (i = 0; i < dev->max_tx_urbs; i++) {
		if (priv->tx_contexts[i].echo_index == dev->max_tx_urbs) {
			context = &priv->tx_contexts[i];

			context->echo_index = i;
			can_put_echo_skb(skb, netdev, context->echo_index);
			++priv->active_tx_contexts;
			if (priv->active_tx_contexts >= dev->max_tx_urbs)
				netif_stop_queue(netdev);

			break;
		}
	}
	spin_unlock_irqrestore(&priv->tx_contexts_lock, flags);

	/* This should never happen; it implies a flow control bug */
	if (!context) {
		netdev_warn(netdev, "cannot find free context\n");

		kfree(buf);
		ret =  NETDEV_TX_BUSY;
		goto freeurb;
	}

	context->priv = priv;
	context->dlc = cf->can_dlc;

	msg->u.tx_can.tid = context->echo_index;

	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev,
					  dev->bulk_out->bEndpointAddress),
			  buf, msg->len,
			  kvaser_usb_write_bulk_callback, context);
	usb_anchor_urb(urb, &priv->tx_submitted);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(err)) {
		spin_lock_irqsave(&priv->tx_contexts_lock, flags);

		can_free_echo_skb(netdev, context->echo_index);
		context->echo_index = dev->max_tx_urbs;
		--priv->active_tx_contexts;
		netif_wake_queue(netdev);

		spin_unlock_irqrestore(&priv->tx_contexts_lock, flags);

		usb_unanchor_urb(urb);
		kfree(buf);

		stats->tx_dropped++;

		if (err == -ENODEV)
			netif_device_detach(netdev);
		else
			netdev_warn(netdev, "Failed tx_urb %d\n", err);

		goto freeurb;
	}

	ret = NETDEV_TX_OK;

freeurb:
	usb_free_urb(urb);
	return ret;
}

static const struct net_device_ops kvaser_usb_netdev_ops = {
	.ndo_open = kvaser_usb_open,
	.ndo_stop = kvaser_usb_close,
	.ndo_start_xmit = kvaser_usb_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct can_bittiming_const kvaser_usb_bittiming_const = {
	.name = "kvaser_usb",
	.tseg1_min = KVASER_USB_TSEG1_MIN,
	.tseg1_max = KVASER_USB_TSEG1_MAX,
	.tseg2_min = KVASER_USB_TSEG2_MIN,
	.tseg2_max = KVASER_USB_TSEG2_MAX,
	.sjw_max = KVASER_USB_SJW_MAX,
	.brp_min = KVASER_USB_BRP_MIN,
	.brp_max = KVASER_USB_BRP_MAX,
	.brp_inc = KVASER_USB_BRP_INC,
};

static int kvaser_usb_set_bittiming(struct net_device *netdev)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct kvaser_usb *dev = priv->dev;
	struct kvaser_msg *msg;
	int rc;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->id = CMD_SET_BUS_PARAMS;
	msg->len = MSG_HEADER_LEN + sizeof(struct kvaser_msg_busparams);
	msg->u.busparams.channel = priv->channel;
	msg->u.busparams.tid = 0xff;
	msg->u.busparams.bitrate = cpu_to_le32(bt->bitrate);
	msg->u.busparams.sjw = bt->sjw;
	msg->u.busparams.tseg1 = bt->prop_seg + bt->phase_seg1;
	msg->u.busparams.tseg2 = bt->phase_seg2;

	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		msg->u.busparams.no_samp = 3;
	else
		msg->u.busparams.no_samp = 1;

	rc = kvaser_usb_send_msg(dev, msg);

	kfree(msg);
	return rc;
}

static int kvaser_usb_set_mode(struct net_device *netdev,
			       enum can_mode mode)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	int err;

	switch (mode) {
	case CAN_MODE_START:
		err = kvaser_usb_simple_msg_async(priv, CMD_START_CHIP);
		if (err)
			return err;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int kvaser_usb_get_berr_counter(const struct net_device *netdev,
				       struct can_berr_counter *bec)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);

	*bec = priv->bec;

	return 0;
}

static void kvaser_usb_remove_interfaces(struct kvaser_usb *dev)
{
	int i;

	for (i = 0; i < dev->nchannels; i++) {
		if (!dev->nets[i])
			continue;

		unregister_candev(dev->nets[i]->netdev);
	}

	kvaser_usb_unlink_all_urbs(dev);

	for (i = 0; i < dev->nchannels; i++) {
		if (!dev->nets[i])
			continue;

		free_candev(dev->nets[i]->netdev);
	}
}

static int kvaser_usb_init_one(struct usb_interface *intf,
			       const struct usb_device_id *id, int channel)
{
	struct kvaser_usb *dev = usb_get_intfdata(intf);
	struct net_device *netdev;
	struct kvaser_usb_net_priv *priv;
	int err;

	err = kvaser_usb_send_simple_msg(dev, CMD_RESET_CHIP, channel);
	if (err)
		return err;

	netdev = alloc_candev(sizeof(*priv) +
			      dev->max_tx_urbs * sizeof(*priv->tx_contexts),
			      dev->max_tx_urbs);
	if (!netdev) {
		dev_err(&intf->dev, "Cannot alloc candev\n");
		return -ENOMEM;
	}

	priv = netdev_priv(netdev);

	init_usb_anchor(&priv->tx_submitted);
	init_completion(&priv->start_comp);
	init_completion(&priv->stop_comp);

	priv->dev = dev;
	priv->netdev = netdev;
	priv->channel = channel;

	spin_lock_init(&priv->tx_contexts_lock);
	kvaser_usb_reset_tx_urb_contexts(priv);

	priv->can.state = CAN_STATE_STOPPED;
	priv->can.clock.freq = CAN_USB_CLOCK;
	priv->can.bittiming_const = &kvaser_usb_bittiming_const;
	priv->can.do_set_bittiming = kvaser_usb_set_bittiming;
	priv->can.do_set_mode = kvaser_usb_set_mode;
	if (id->driver_info & KVASER_HAS_TXRX_ERRORS)
		priv->can.do_get_berr_counter = kvaser_usb_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES;
	if (id->driver_info & KVASER_HAS_SILENT_MODE)
		priv->can.ctrlmode_supported |= CAN_CTRLMODE_LISTENONLY;

	netdev->flags |= IFF_ECHO;

	netdev->netdev_ops = &kvaser_usb_netdev_ops;

	SET_NETDEV_DEV(netdev, &intf->dev);
	netdev->dev_id = channel;

	dev->nets[channel] = priv;

	err = register_candev(netdev);
	if (err) {
		dev_err(&intf->dev, "Failed to register can device\n");
		free_candev(netdev);
		dev->nets[channel] = NULL;
		return err;
	}

	netdev_dbg(netdev, "device registered\n");

	return 0;
}

static int kvaser_usb_get_endpoints(const struct usb_interface *intf,
				    struct usb_endpoint_descriptor **in,
				    struct usb_endpoint_descriptor **out)
{
	const struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i;

	iface_desc = &intf->altsetting[0];

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!*in && usb_endpoint_is_bulk_in(endpoint))
			*in = endpoint;

		if (!*out && usb_endpoint_is_bulk_out(endpoint))
			*out = endpoint;

		/* use first bulk endpoint for in and out */
		if (*in && *out)
			return 0;
	}

	return -ENODEV;
}

static int kvaser_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	struct kvaser_usb *dev;
	int err = -ENOMEM;
	int i, retry = 3;

	dev = devm_kzalloc(&intf->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	if (kvaser_is_leaf(id)) {
		dev->family = KVASER_LEAF;
	} else if (kvaser_is_usbcan(id)) {
		dev->family = KVASER_USBCAN;
	} else {
		dev_err(&intf->dev,
			"Product ID (%d) does not belong to any known Kvaser USB family",
			id->idProduct);
		return -ENODEV;
	}

	err = kvaser_usb_get_endpoints(intf, &dev->bulk_in, &dev->bulk_out);
	if (err) {
		dev_err(&intf->dev, "Cannot get usb endpoint(s)");
		return err;
	}

	dev->udev = interface_to_usbdev(intf);

	init_usb_anchor(&dev->rx_submitted);

	usb_set_intfdata(intf, dev);

	/* On some x86 laptops, plugging a Kvaser device again after
	 * an unplug makes the firmware always ignore the very first
	 * command. For such a case, provide some room for retries
	 * instead of completely exiting the driver.
	 */
	do {
		err = kvaser_usb_get_software_info(dev);
	} while (--retry && err == -ETIMEDOUT);

	if (err) {
		dev_err(&intf->dev,
			"Cannot get software infos, error %d\n", err);
		return err;
	}

	dev_dbg(&intf->dev, "Firmware version: %d.%d.%d\n",
		((dev->fw_version >> 24) & 0xff),
		((dev->fw_version >> 16) & 0xff),
		(dev->fw_version & 0xffff));

	dev_dbg(&intf->dev, "Max outstanding tx = %d URBs\n", dev->max_tx_urbs);

	err = kvaser_usb_get_card_info(dev);
	if (err) {
		dev_err(&intf->dev,
			"Cannot get card infos, error %d\n", err);
		return err;
	}

	for (i = 0; i < dev->nchannels; i++) {
		err = kvaser_usb_init_one(intf, id, i);
		if (err) {
			kvaser_usb_remove_interfaces(dev);
			return err;
		}
	}

	return 0;
}

static void kvaser_usb_disconnect(struct usb_interface *intf)
{
	struct kvaser_usb *dev = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (!dev)
		return;

	kvaser_usb_remove_interfaces(dev);
}

static struct usb_driver kvaser_usb_driver = {
	.name = "kvaser_usb",
	.probe = kvaser_usb_probe,
	.disconnect = kvaser_usb_disconnect,
	.id_table = kvaser_usb_table,
};

module_usb_driver(kvaser_usb_driver);

MODULE_AUTHOR("Olivier Sobrie <olivier@sobrie.be>");
MODULE_DESCRIPTION("CAN driver for Kvaser CAN/USB devices");
MODULE_LICENSE("GPL v2");
