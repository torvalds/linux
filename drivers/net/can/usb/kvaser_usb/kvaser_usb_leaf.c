// SPDX-License-Identifier: GPL-2.0
/* Parts of this driver are based on the following:
 *  - Kvaser linux leaf driver (version 4.78)
 *  - CAN driver for esd CAN-USB/2
 *  - Kvaser linux usbcanII driver (version 5.3)
 *
 * Copyright (C) 2002-2018 KVASER AB, Sweden. All rights reserved.
 * Copyright (C) 2010 Matthias Fuchs <matthias.fuchs@esd.eu>, esd gmbh
 * Copyright (C) 2012 Olivier Sobrie <olivier@sobrie.be>
 * Copyright (C) 2015 Valeo S.A.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/netlink.h>

#include "kvaser_usb.h"

#define MAX_USBCAN_NET_DEVICES		2

/* Command header size */
#define CMD_HEADER_LEN			2

/* Kvaser CAN message flags */
#define MSG_FLAG_ERROR_FRAME		BIT(0)
#define MSG_FLAG_OVERRUN		BIT(1)
#define MSG_FLAG_NERR			BIT(2)
#define MSG_FLAG_WAKEUP			BIT(3)
#define MSG_FLAG_REMOTE_FRAME		BIT(4)
#define MSG_FLAG_RESERVED		BIT(5)
#define MSG_FLAG_TX_ACK			BIT(6)
#define MSG_FLAG_TX_REQUEST		BIT(7)

/* CAN states (M16C CxSTRH register) */
#define M16C_STATE_BUS_RESET		BIT(0)
#define M16C_STATE_BUS_ERROR		BIT(4)
#define M16C_STATE_BUS_PASSIVE		BIT(5)
#define M16C_STATE_BUS_OFF		BIT(6)

/* Leaf/usbcan command ids */
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
#define CMD_RESET_CHIP			24
#define CMD_START_CHIP			26
#define CMD_START_CHIP_REPLY		27
#define CMD_STOP_CHIP			28
#define CMD_STOP_CHIP_REPLY		29

#define CMD_USBCAN_CLOCK_OVERFLOW_EVENT	33

#define CMD_GET_CARD_INFO		34
#define CMD_GET_CARD_INFO_REPLY		35
#define CMD_GET_SOFTWARE_INFO		38
#define CMD_GET_SOFTWARE_INFO_REPLY	39
#define CMD_ERROR_EVENT			45
#define CMD_FLUSH_QUEUE			48
#define CMD_TX_ACKNOWLEDGE		50
#define CMD_CAN_ERROR_EVENT		51
#define CMD_FLUSH_QUEUE_REPLY		68
#define CMD_GET_CAPABILITIES_REQ	95
#define CMD_GET_CAPABILITIES_RESP	96

#define CMD_LEAF_LOG_MESSAGE		106

/* Leaf frequency options */
#define KVASER_USB_LEAF_SWOPTION_FREQ_MASK 0x60
#define KVASER_USB_LEAF_SWOPTION_FREQ_16_MHZ_CLK 0
#define KVASER_USB_LEAF_SWOPTION_FREQ_32_MHZ_CLK BIT(5)
#define KVASER_USB_LEAF_SWOPTION_FREQ_24_MHZ_CLK BIT(6)

#define KVASER_USB_LEAF_SWOPTION_EXT_CAP BIT(12)

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

/* ctrl modes */
#define KVASER_CTRL_MODE_NORMAL		1
#define KVASER_CTRL_MODE_SILENT		2
#define KVASER_CTRL_MODE_SELFRECEPTION	3
#define KVASER_CTRL_MODE_OFF		4

/* Extended CAN identifier flag */
#define KVASER_EXTENDED_FRAME		BIT(31)

struct kvaser_cmd_simple {
	u8 tid;
	u8 channel;
} __packed;

struct kvaser_cmd_cardinfo {
	u8 tid;
	u8 nchannels;
	__le32 serial_number;
	__le32 padding0;
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
	__le16 padding1;
} __packed;

struct leaf_cmd_softinfo {
	u8 tid;
	u8 padding0;
	__le32 sw_options;
	__le32 fw_version;
	__le16 max_outstanding_tx;
	__le16 padding1[9];
} __packed;

struct usbcan_cmd_softinfo {
	u8 tid;
	u8 fw_name[5];
	__le16 max_outstanding_tx;
	u8 padding[6];
	__le32 fw_version;
	__le16 checksum;
	__le16 sw_options;
} __packed;

struct kvaser_cmd_busparams {
	u8 tid;
	u8 channel;
	struct kvaser_usb_busparams busparams;
} __packed;

struct kvaser_cmd_tx_can {
	u8 channel;
	u8 tid;
	u8 data[14];
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

struct kvaser_cmd_rx_can_header {
	u8 channel;
	u8 flag;
} __packed;

struct leaf_cmd_rx_can {
	u8 channel;
	u8 flag;

	__le16 time[3];
	u8 data[14];
} __packed;

struct usbcan_cmd_rx_can {
	u8 channel;
	u8 flag;

	u8 data[14];
	__le16 time;
} __packed;

struct leaf_cmd_chip_state_event {
	u8 tid;
	u8 channel;

	__le16 time[3];
	u8 tx_errors_count;
	u8 rx_errors_count;

	u8 status;
	u8 padding[3];
} __packed;

struct usbcan_cmd_chip_state_event {
	u8 tid;
	u8 channel;

	u8 tx_errors_count;
	u8 rx_errors_count;
	__le16 time;

	u8 status;
	u8 padding[3];
} __packed;

struct kvaser_cmd_tx_acknowledge_header {
	u8 channel;
	u8 tid;
} __packed;

struct leaf_cmd_can_error_event {
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

struct usbcan_cmd_can_error_event {
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

/* CMD_ERROR_EVENT error codes */
#define KVASER_USB_LEAF_ERROR_EVENT_TX_QUEUE_FULL 0x8
#define KVASER_USB_LEAF_ERROR_EVENT_PARAM 0x9

struct leaf_cmd_error_event {
	u8 tid;
	u8 error_code;
	__le16 timestamp[3];
	__le16 padding;
	__le16 info1;
	__le16 info2;
} __packed;

struct usbcan_cmd_error_event {
	u8 tid;
	u8 error_code;
	__le16 info1;
	__le16 info2;
	__le16 timestamp;
	__le16 padding;
} __packed;

struct kvaser_cmd_ctrl_mode {
	u8 tid;
	u8 channel;
	u8 ctrl_mode;
	u8 padding[3];
} __packed;

struct kvaser_cmd_flush_queue {
	u8 tid;
	u8 channel;
	u8 flags;
	u8 padding[3];
} __packed;

struct leaf_cmd_log_message {
	u8 channel;
	u8 flags;
	__le16 time[3];
	u8 dlc;
	u8 time_offset;
	__le32 id;
	u8 data[8];
} __packed;

/* Sub commands for cap_req and cap_res */
#define KVASER_USB_LEAF_CAP_CMD_LISTEN_MODE 0x02
#define KVASER_USB_LEAF_CAP_CMD_ERR_REPORT 0x05
struct kvaser_cmd_cap_req {
	__le16 padding0;
	__le16 cap_cmd;
	__le16 padding1;
	__le16 channel;
} __packed;

/* Status codes for cap_res */
#define KVASER_USB_LEAF_CAP_STAT_OK 0x00
#define KVASER_USB_LEAF_CAP_STAT_NOT_IMPL 0x01
#define KVASER_USB_LEAF_CAP_STAT_UNAVAIL 0x02
struct kvaser_cmd_cap_res {
	__le16 padding;
	__le16 cap_cmd;
	__le16 status;
	__le32 mask;
	__le32 value;
} __packed;

struct kvaser_cmd {
	u8 len;
	u8 id;
	union	{
		struct kvaser_cmd_simple simple;
		struct kvaser_cmd_cardinfo cardinfo;
		struct kvaser_cmd_busparams busparams;

		struct kvaser_cmd_rx_can_header rx_can_header;
		struct kvaser_cmd_tx_acknowledge_header tx_acknowledge_header;

		union {
			struct leaf_cmd_softinfo softinfo;
			struct leaf_cmd_rx_can rx_can;
			struct leaf_cmd_chip_state_event chip_state_event;
			struct leaf_cmd_can_error_event can_error_event;
			struct leaf_cmd_log_message log_message;
			struct leaf_cmd_error_event error_event;
			struct kvaser_cmd_cap_req cap_req;
			struct kvaser_cmd_cap_res cap_res;
		} __packed leaf;

		union {
			struct usbcan_cmd_softinfo softinfo;
			struct usbcan_cmd_rx_can rx_can;
			struct usbcan_cmd_chip_state_event chip_state_event;
			struct usbcan_cmd_can_error_event can_error_event;
			struct usbcan_cmd_error_event error_event;
		} __packed usbcan;

		struct kvaser_cmd_tx_can tx_can;
		struct kvaser_cmd_ctrl_mode ctrl_mode;
		struct kvaser_cmd_flush_queue flush_queue;
	} u;
} __packed;

#define CMD_SIZE_ANY 0xff
#define kvaser_fsize(field) sizeof_field(struct kvaser_cmd, field)

static const u8 kvaser_usb_leaf_cmd_sizes_leaf[] = {
	[CMD_START_CHIP_REPLY]		= kvaser_fsize(u.simple),
	[CMD_STOP_CHIP_REPLY]		= kvaser_fsize(u.simple),
	[CMD_GET_CARD_INFO_REPLY]	= kvaser_fsize(u.cardinfo),
	[CMD_TX_ACKNOWLEDGE]		= kvaser_fsize(u.tx_acknowledge_header),
	[CMD_GET_SOFTWARE_INFO_REPLY]	= kvaser_fsize(u.leaf.softinfo),
	[CMD_RX_STD_MESSAGE]		= kvaser_fsize(u.leaf.rx_can),
	[CMD_RX_EXT_MESSAGE]		= kvaser_fsize(u.leaf.rx_can),
	[CMD_LEAF_LOG_MESSAGE]		= kvaser_fsize(u.leaf.log_message),
	[CMD_CHIP_STATE_EVENT]		= kvaser_fsize(u.leaf.chip_state_event),
	[CMD_CAN_ERROR_EVENT]		= kvaser_fsize(u.leaf.can_error_event),
	[CMD_GET_CAPABILITIES_RESP]	= kvaser_fsize(u.leaf.cap_res),
	[CMD_GET_BUS_PARAMS_REPLY]	= kvaser_fsize(u.busparams),
	[CMD_ERROR_EVENT]		= kvaser_fsize(u.leaf.error_event),
	/* ignored events: */
	[CMD_FLUSH_QUEUE_REPLY]		= CMD_SIZE_ANY,
};

static const u8 kvaser_usb_leaf_cmd_sizes_usbcan[] = {
	[CMD_START_CHIP_REPLY]		= kvaser_fsize(u.simple),
	[CMD_STOP_CHIP_REPLY]		= kvaser_fsize(u.simple),
	[CMD_GET_CARD_INFO_REPLY]	= kvaser_fsize(u.cardinfo),
	[CMD_TX_ACKNOWLEDGE]		= kvaser_fsize(u.tx_acknowledge_header),
	[CMD_GET_SOFTWARE_INFO_REPLY]	= kvaser_fsize(u.usbcan.softinfo),
	[CMD_RX_STD_MESSAGE]		= kvaser_fsize(u.usbcan.rx_can),
	[CMD_RX_EXT_MESSAGE]		= kvaser_fsize(u.usbcan.rx_can),
	[CMD_CHIP_STATE_EVENT]		= kvaser_fsize(u.usbcan.chip_state_event),
	[CMD_CAN_ERROR_EVENT]		= kvaser_fsize(u.usbcan.can_error_event),
	[CMD_ERROR_EVENT]		= kvaser_fsize(u.usbcan.error_event),
	/* ignored events: */
	[CMD_USBCAN_CLOCK_OVERFLOW_EVENT] = CMD_SIZE_ANY,
};

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
struct kvaser_usb_err_summary {
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

struct kvaser_usb_net_leaf_priv {
	struct kvaser_usb_net_priv *net;

	struct delayed_work chip_state_req_work;
};

static const struct can_bittiming_const kvaser_usb_leaf_m16c_bittiming_const = {
	.name = "kvaser_usb_ucii",
	.tseg1_min = 4,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 16,
	.brp_inc = 1,
};

static const struct can_bittiming_const kvaser_usb_leaf_m32c_bittiming_const = {
	.name = "kvaser_usb_leaf",
	.tseg1_min = 3,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 2,
	.brp_max = 128,
	.brp_inc = 2,
};

static const struct kvaser_usb_dev_cfg kvaser_usb_leaf_usbcan_dev_cfg = {
	.clock = {
		.freq = 8 * MEGA /* Hz */,
	},
	.timestamp_freq = 1,
	.bittiming_const = &kvaser_usb_leaf_m16c_bittiming_const,
};

static const struct kvaser_usb_dev_cfg kvaser_usb_leaf_m32c_dev_cfg = {
	.clock = {
		.freq = 16 * MEGA /* Hz */,
	},
	.timestamp_freq = 1,
	.bittiming_const = &kvaser_usb_leaf_m32c_bittiming_const,
};

static const struct kvaser_usb_dev_cfg kvaser_usb_leaf_imx_dev_cfg_16mhz = {
	.clock = {
		.freq = 16 * MEGA /* Hz */,
	},
	.timestamp_freq = 1,
	.bittiming_const = &kvaser_usb_flexc_bittiming_const,
};

static const struct kvaser_usb_dev_cfg kvaser_usb_leaf_imx_dev_cfg_24mhz = {
	.clock = {
		.freq = 24 * MEGA /* Hz */,
	},
	.timestamp_freq = 1,
	.bittiming_const = &kvaser_usb_flexc_bittiming_const,
};

static const struct kvaser_usb_dev_cfg kvaser_usb_leaf_imx_dev_cfg_32mhz = {
	.clock = {
		.freq = 32 * MEGA /* Hz */,
	},
	.timestamp_freq = 1,
	.bittiming_const = &kvaser_usb_flexc_bittiming_const,
};

static int kvaser_usb_leaf_verify_size(const struct kvaser_usb *dev,
				       const struct kvaser_cmd *cmd)
{
	/* buffer size >= cmd->len ensured by caller */
	u8 min_size = 0;

	switch (dev->driver_info->family) {
	case KVASER_LEAF:
		if (cmd->id < ARRAY_SIZE(kvaser_usb_leaf_cmd_sizes_leaf))
			min_size = kvaser_usb_leaf_cmd_sizes_leaf[cmd->id];
		break;
	case KVASER_USBCAN:
		if (cmd->id < ARRAY_SIZE(kvaser_usb_leaf_cmd_sizes_usbcan))
			min_size = kvaser_usb_leaf_cmd_sizes_usbcan[cmd->id];
		break;
	}

	if (min_size == CMD_SIZE_ANY)
		return 0;

	if (min_size) {
		min_size += CMD_HEADER_LEN;
		if (cmd->len >= min_size)
			return 0;

		dev_err_ratelimited(&dev->intf->dev,
				    "Received command %u too short (size %u, needed %u)",
				    cmd->id, cmd->len, min_size);
		return -EIO;
	}

	dev_warn_ratelimited(&dev->intf->dev,
			     "Unhandled command (%d, size %d)\n",
			     cmd->id, cmd->len);
	return -EINVAL;
}

static void *
kvaser_usb_leaf_frame_to_cmd(const struct kvaser_usb_net_priv *priv,
			     const struct sk_buff *skb, int *cmd_len,
			     u16 transid)
{
	struct kvaser_usb *dev = priv->dev;
	struct kvaser_cmd *cmd;
	u8 *cmd_tx_can_flags = NULL;		/* GCC */
	struct can_frame *cf = (struct can_frame *)skb->data;

	cmd = kmalloc(sizeof(*cmd), GFP_ATOMIC);
	if (cmd) {
		cmd->u.tx_can.tid = transid & 0xff;
		cmd->len = *cmd_len = CMD_HEADER_LEN +
				      sizeof(struct kvaser_cmd_tx_can);
		cmd->u.tx_can.channel = priv->channel;

		switch (dev->driver_info->family) {
		case KVASER_LEAF:
			cmd_tx_can_flags = &cmd->u.tx_can.leaf.flags;
			break;
		case KVASER_USBCAN:
			cmd_tx_can_flags = &cmd->u.tx_can.usbcan.flags;
			break;
		}

		*cmd_tx_can_flags = 0;

		if (cf->can_id & CAN_EFF_FLAG) {
			cmd->id = CMD_TX_EXT_MESSAGE;
			cmd->u.tx_can.data[0] = (cf->can_id >> 24) & 0x1f;
			cmd->u.tx_can.data[1] = (cf->can_id >> 18) & 0x3f;
			cmd->u.tx_can.data[2] = (cf->can_id >> 14) & 0x0f;
			cmd->u.tx_can.data[3] = (cf->can_id >> 6) & 0xff;
			cmd->u.tx_can.data[4] = cf->can_id & 0x3f;
		} else {
			cmd->id = CMD_TX_STD_MESSAGE;
			cmd->u.tx_can.data[0] = (cf->can_id >> 6) & 0x1f;
			cmd->u.tx_can.data[1] = cf->can_id & 0x3f;
		}

		cmd->u.tx_can.data[5] = cf->len;
		memcpy(&cmd->u.tx_can.data[6], cf->data, cf->len);

		if (cf->can_id & CAN_RTR_FLAG)
			*cmd_tx_can_flags |= MSG_FLAG_REMOTE_FRAME;
	}
	return cmd;
}

static int kvaser_usb_leaf_wait_cmd(const struct kvaser_usb *dev, u8 id,
				    struct kvaser_cmd *cmd)
{
	struct kvaser_cmd *tmp;
	void *buf;
	int actual_len;
	int err;
	int pos;
	unsigned long to = jiffies + msecs_to_jiffies(KVASER_USB_TIMEOUT);

	buf = kzalloc(KVASER_USB_RX_BUFFER_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	do {
		err = kvaser_usb_recv_cmd(dev, buf, KVASER_USB_RX_BUFFER_SIZE,
					  &actual_len);
		if (err < 0)
			goto end;

		pos = 0;
		while (pos <= actual_len - CMD_HEADER_LEN) {
			tmp = buf + pos;

			/* Handle commands crossing the USB endpoint max packet
			 * size boundary. Check kvaser_usb_read_bulk_callback()
			 * for further details.
			 */
			if (tmp->len == 0) {
				pos = round_up(pos,
					       le16_to_cpu
						(dev->bulk_in->wMaxPacketSize));
				continue;
			}

			if (pos + tmp->len > actual_len) {
				dev_err_ratelimited(&dev->intf->dev,
						    "Format error\n");
				break;
			}

			if (tmp->id == id) {
				memcpy(cmd, tmp, tmp->len);
				goto end;
			}

			pos += tmp->len;
		}
	} while (time_before(jiffies, to));

	err = -EINVAL;

end:
	kfree(buf);

	if (err == 0)
		err = kvaser_usb_leaf_verify_size(dev, cmd);

	return err;
}

static int kvaser_usb_leaf_send_simple_cmd(const struct kvaser_usb *dev,
					   u8 cmd_id, int channel)
{
	struct kvaser_cmd *cmd;
	int rc;

	cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->id = cmd_id;
	cmd->len = CMD_HEADER_LEN + sizeof(struct kvaser_cmd_simple);
	cmd->u.simple.channel = channel;
	cmd->u.simple.tid = 0xff;

	rc = kvaser_usb_send_cmd(dev, cmd, cmd->len);

	kfree(cmd);
	return rc;
}

static void kvaser_usb_leaf_get_software_info_leaf(struct kvaser_usb *dev,
						   const struct leaf_cmd_softinfo *softinfo)
{
	u32 sw_options = le32_to_cpu(softinfo->sw_options);

	dev->fw_version = le32_to_cpu(softinfo->fw_version);
	dev->max_tx_urbs = le16_to_cpu(softinfo->max_outstanding_tx);

	if (sw_options & KVASER_USB_LEAF_SWOPTION_EXT_CAP)
		dev->card_data.capabilities |= KVASER_USB_CAP_EXT_CAP;

	if (dev->driver_info->quirks & KVASER_USB_QUIRK_IGNORE_CLK_FREQ) {
		/* Firmware expects bittiming parameters calculated for 16MHz
		 * clock, regardless of the actual clock
		 */
		dev->cfg = &kvaser_usb_leaf_m32c_dev_cfg;
	} else {
		switch (sw_options & KVASER_USB_LEAF_SWOPTION_FREQ_MASK) {
		case KVASER_USB_LEAF_SWOPTION_FREQ_16_MHZ_CLK:
			dev->cfg = &kvaser_usb_leaf_imx_dev_cfg_16mhz;
			break;
		case KVASER_USB_LEAF_SWOPTION_FREQ_24_MHZ_CLK:
			dev->cfg = &kvaser_usb_leaf_imx_dev_cfg_24mhz;
			break;
		case KVASER_USB_LEAF_SWOPTION_FREQ_32_MHZ_CLK:
			dev->cfg = &kvaser_usb_leaf_imx_dev_cfg_32mhz;
			break;
		}
	}
}

static int kvaser_usb_leaf_get_software_info_inner(struct kvaser_usb *dev)
{
	struct kvaser_cmd cmd;
	int err;

	err = kvaser_usb_leaf_send_simple_cmd(dev, CMD_GET_SOFTWARE_INFO, 0);
	if (err)
		return err;

	err = kvaser_usb_leaf_wait_cmd(dev, CMD_GET_SOFTWARE_INFO_REPLY, &cmd);
	if (err)
		return err;

	switch (dev->driver_info->family) {
	case KVASER_LEAF:
		kvaser_usb_leaf_get_software_info_leaf(dev, &cmd.u.leaf.softinfo);
		break;
	case KVASER_USBCAN:
		dev->fw_version = le32_to_cpu(cmd.u.usbcan.softinfo.fw_version);
		dev->max_tx_urbs =
			le16_to_cpu(cmd.u.usbcan.softinfo.max_outstanding_tx);
		dev->cfg = &kvaser_usb_leaf_usbcan_dev_cfg;
		break;
	}

	return 0;
}

static int kvaser_usb_leaf_get_software_info(struct kvaser_usb *dev)
{
	int err;
	int retry = 3;

	/* On some x86 laptops, plugging a Kvaser device again after
	 * an unplug makes the firmware always ignore the very first
	 * command. For such a case, provide some room for retries
	 * instead of completely exiting the driver.
	 */
	do {
		err = kvaser_usb_leaf_get_software_info_inner(dev);
	} while (--retry && err == -ETIMEDOUT);

	return err;
}

static int kvaser_usb_leaf_get_card_info(struct kvaser_usb *dev)
{
	struct kvaser_cmd cmd;
	int err;

	err = kvaser_usb_leaf_send_simple_cmd(dev, CMD_GET_CARD_INFO, 0);
	if (err)
		return err;

	err = kvaser_usb_leaf_wait_cmd(dev, CMD_GET_CARD_INFO_REPLY, &cmd);
	if (err)
		return err;

	dev->nchannels = cmd.u.cardinfo.nchannels;
	if (dev->nchannels > KVASER_USB_MAX_NET_DEVICES ||
	    (dev->driver_info->family == KVASER_USBCAN &&
	     dev->nchannels > MAX_USBCAN_NET_DEVICES))
		return -EINVAL;

	return 0;
}

static int kvaser_usb_leaf_get_single_capability(struct kvaser_usb *dev,
						 u16 cap_cmd_req, u16 *status)
{
	struct kvaser_usb_dev_card_data *card_data = &dev->card_data;
	struct kvaser_cmd *cmd;
	u32 value = 0;
	u32 mask = 0;
	u16 cap_cmd_res;
	int err;
	int i;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->id = CMD_GET_CAPABILITIES_REQ;
	cmd->u.leaf.cap_req.cap_cmd = cpu_to_le16(cap_cmd_req);
	cmd->len = CMD_HEADER_LEN + sizeof(struct kvaser_cmd_cap_req);

	err = kvaser_usb_send_cmd(dev, cmd, cmd->len);
	if (err)
		goto end;

	err = kvaser_usb_leaf_wait_cmd(dev, CMD_GET_CAPABILITIES_RESP, cmd);
	if (err)
		goto end;

	*status = le16_to_cpu(cmd->u.leaf.cap_res.status);

	if (*status != KVASER_USB_LEAF_CAP_STAT_OK)
		goto end;

	cap_cmd_res = le16_to_cpu(cmd->u.leaf.cap_res.cap_cmd);
	switch (cap_cmd_res) {
	case KVASER_USB_LEAF_CAP_CMD_LISTEN_MODE:
	case KVASER_USB_LEAF_CAP_CMD_ERR_REPORT:
		value = le32_to_cpu(cmd->u.leaf.cap_res.value);
		mask = le32_to_cpu(cmd->u.leaf.cap_res.mask);
		break;
	default:
		dev_warn(&dev->intf->dev, "Unknown capability command %u\n",
			 cap_cmd_res);
		break;
	}

	for (i = 0; i < dev->nchannels; i++) {
		if (BIT(i) & (value & mask)) {
			switch (cap_cmd_res) {
			case KVASER_USB_LEAF_CAP_CMD_LISTEN_MODE:
				card_data->ctrlmode_supported |=
						CAN_CTRLMODE_LISTENONLY;
				break;
			case KVASER_USB_LEAF_CAP_CMD_ERR_REPORT:
				card_data->capabilities |=
						KVASER_USB_CAP_BERR_CAP;
				break;
			}
		}
	}

end:
	kfree(cmd);

	return err;
}

static int kvaser_usb_leaf_get_capabilities_leaf(struct kvaser_usb *dev)
{
	int err;
	u16 status;

	if (!(dev->card_data.capabilities & KVASER_USB_CAP_EXT_CAP)) {
		dev_info(&dev->intf->dev,
			 "No extended capability support. Upgrade device firmware.\n");
		return 0;
	}

	err = kvaser_usb_leaf_get_single_capability(dev,
						    KVASER_USB_LEAF_CAP_CMD_LISTEN_MODE,
						    &status);
	if (err)
		return err;
	if (status)
		dev_info(&dev->intf->dev,
			 "KVASER_USB_LEAF_CAP_CMD_LISTEN_MODE failed %u\n",
			 status);

	err = kvaser_usb_leaf_get_single_capability(dev,
						    KVASER_USB_LEAF_CAP_CMD_ERR_REPORT,
						    &status);
	if (err)
		return err;
	if (status)
		dev_info(&dev->intf->dev,
			 "KVASER_USB_LEAF_CAP_CMD_ERR_REPORT failed %u\n",
			 status);

	return 0;
}

static int kvaser_usb_leaf_get_capabilities(struct kvaser_usb *dev)
{
	int err = 0;

	if (dev->driver_info->family == KVASER_LEAF)
		err = kvaser_usb_leaf_get_capabilities_leaf(dev);

	return err;
}

static void kvaser_usb_leaf_tx_acknowledge(const struct kvaser_usb *dev,
					   const struct kvaser_cmd *cmd)
{
	struct net_device_stats *stats;
	struct kvaser_usb_tx_urb_context *context;
	struct kvaser_usb_net_priv *priv;
	unsigned long flags;
	u8 channel, tid;

	channel = cmd->u.tx_acknowledge_header.channel;
	tid = cmd->u.tx_acknowledge_header.tid;

	if (channel >= dev->nchannels) {
		dev_err(&dev->intf->dev,
			"Invalid channel number (%d)\n", channel);
		return;
	}

	priv = dev->nets[channel];

	if (!netif_device_present(priv->netdev))
		return;

	stats = &priv->netdev->stats;

	context = &priv->tx_contexts[tid % dev->max_tx_urbs];

	/* Sometimes the state change doesn't come after a bus-off event */
	if (priv->can.restart_ms && priv->can.state == CAN_STATE_BUS_OFF) {
		struct sk_buff *skb;
		struct can_frame *cf;

		skb = alloc_can_err_skb(priv->netdev, &cf);
		if (skb) {
			cf->can_id |= CAN_ERR_RESTARTED;

			netif_rx(skb);
		} else {
			netdev_err(priv->netdev,
				   "No memory left for err_skb\n");
		}

		priv->can.can_stats.restarts++;
		netif_carrier_on(priv->netdev);

		priv->can.state = CAN_STATE_ERROR_ACTIVE;
	}

	spin_lock_irqsave(&priv->tx_contexts_lock, flags);

	stats->tx_packets++;
	stats->tx_bytes += can_get_echo_skb(priv->netdev,
					    context->echo_index, NULL);
	context->echo_index = dev->max_tx_urbs;
	--priv->active_tx_contexts;
	netif_wake_queue(priv->netdev);

	spin_unlock_irqrestore(&priv->tx_contexts_lock, flags);
}

static int kvaser_usb_leaf_simple_cmd_async(struct kvaser_usb_net_priv *priv,
					    u8 cmd_id)
{
	struct kvaser_cmd *cmd;
	int err;

	cmd = kzalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd)
		return -ENOMEM;

	cmd->len = CMD_HEADER_LEN + sizeof(struct kvaser_cmd_simple);
	cmd->id = cmd_id;
	cmd->u.simple.channel = priv->channel;

	err = kvaser_usb_send_cmd_async(priv, cmd, cmd->len);
	if (err)
		kfree(cmd);

	return err;
}

static void kvaser_usb_leaf_chip_state_req_work(struct work_struct *work)
{
	struct kvaser_usb_net_leaf_priv *leaf =
		container_of(work, struct kvaser_usb_net_leaf_priv,
			     chip_state_req_work.work);
	struct kvaser_usb_net_priv *priv = leaf->net;

	kvaser_usb_leaf_simple_cmd_async(priv, CMD_GET_CHIP_STATE);
}

static void
kvaser_usb_leaf_rx_error_update_can_state(struct kvaser_usb_net_priv *priv,
					const struct kvaser_usb_err_summary *es,
					struct can_frame *cf)
{
	struct kvaser_usb *dev = priv->dev;
	struct net_device_stats *stats = &priv->netdev->stats;
	enum can_state cur_state, new_state, tx_state, rx_state;

	netdev_dbg(priv->netdev, "Error status: 0x%02x\n", es->status);

	new_state = priv->can.state;
	cur_state = priv->can.state;

	if (es->status & (M16C_STATE_BUS_OFF | M16C_STATE_BUS_RESET)) {
		new_state = CAN_STATE_BUS_OFF;
	} else if (es->status & M16C_STATE_BUS_PASSIVE) {
		new_state = CAN_STATE_ERROR_PASSIVE;
	} else if ((es->status & M16C_STATE_BUS_ERROR) &&
		   cur_state >= CAN_STATE_BUS_OFF) {
		/* Guard against spurious error events after a busoff */
	} else if (es->txerr >= 128 || es->rxerr >= 128) {
		new_state = CAN_STATE_ERROR_PASSIVE;
	} else if (es->txerr >= 96 || es->rxerr >= 96) {
		new_state = CAN_STATE_ERROR_WARNING;
	} else {
		new_state = CAN_STATE_ERROR_ACTIVE;
	}

	if (new_state != cur_state) {
		tx_state = (es->txerr >= es->rxerr) ? new_state : 0;
		rx_state = (es->txerr <= es->rxerr) ? new_state : 0;

		can_change_state(priv->netdev, cf, tx_state, rx_state);
	}

	if (priv->can.restart_ms &&
	    cur_state == CAN_STATE_BUS_OFF &&
	    new_state < CAN_STATE_BUS_OFF)
		priv->can.can_stats.restarts++;

	switch (dev->driver_info->family) {
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
		if (es->usbcan.error_state & USBCAN_ERROR_STATE_BUSERROR)
			priv->can.can_stats.bus_error++;
		break;
	}

	priv->bec.txerr = es->txerr;
	priv->bec.rxerr = es->rxerr;
}

static void kvaser_usb_leaf_rx_error(const struct kvaser_usb *dev,
				     const struct kvaser_usb_err_summary *es)
{
	struct can_frame *cf;
	struct can_frame tmp_cf = { .can_id = CAN_ERR_FLAG,
				    .len = CAN_ERR_DLC };
	struct sk_buff *skb;
	struct net_device_stats *stats;
	struct kvaser_usb_net_priv *priv;
	struct kvaser_usb_net_leaf_priv *leaf;
	enum can_state old_state, new_state;

	if (es->channel >= dev->nchannels) {
		dev_err(&dev->intf->dev,
			"Invalid channel number (%d)\n", es->channel);
		return;
	}

	priv = dev->nets[es->channel];
	leaf = priv->sub_priv;
	stats = &priv->netdev->stats;

	/* Ignore e.g. state change to bus-off reported just after stopping */
	if (!netif_running(priv->netdev))
		return;

	/* Update all of the CAN interface's state and error counters before
	 * trying any memory allocation that can actually fail with -ENOMEM.
	 *
	 * We send a temporary stack-allocated error CAN frame to
	 * can_change_state() for the very same reason.
	 *
	 * TODO: Split can_change_state() responsibility between updating the
	 * CAN interface's state and counters, and the setting up of CAN error
	 * frame ID and data to userspace. Remove stack allocation afterwards.
	 */
	old_state = priv->can.state;
	kvaser_usb_leaf_rx_error_update_can_state(priv, es, &tmp_cf);
	new_state = priv->can.state;

	/* If there are errors, request status updates periodically as we do
	 * not get automatic notifications of improved state.
	 */
	if (new_state < CAN_STATE_BUS_OFF &&
	    (es->rxerr || es->txerr || new_state == CAN_STATE_ERROR_PASSIVE))
		schedule_delayed_work(&leaf->chip_state_req_work,
				      msecs_to_jiffies(500));

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
				kvaser_usb_leaf_simple_cmd_async(priv,
								 CMD_STOP_CHIP);
			netif_carrier_off(priv->netdev);
		}

		if (priv->can.restart_ms &&
		    old_state == CAN_STATE_BUS_OFF &&
		    new_state < CAN_STATE_BUS_OFF) {
			cf->can_id |= CAN_ERR_RESTARTED;
			netif_carrier_on(priv->netdev);
		}
	}

	switch (dev->driver_info->family) {
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
		if (es->usbcan.error_state & USBCAN_ERROR_STATE_BUSERROR)
			cf->can_id |= CAN_ERR_BUSERROR;
		break;
	}

	if (new_state != CAN_STATE_BUS_OFF) {
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = es->txerr;
		cf->data[7] = es->rxerr;
	}

	netif_rx(skb);
}

/* For USBCAN, report error to userspace if the channels's errors counter
 * has changed, or we're the only channel seeing a bus error state.
 */
static void
kvaser_usb_leaf_usbcan_conditionally_rx_error(const struct kvaser_usb *dev,
					      struct kvaser_usb_err_summary *es)
{
	struct kvaser_usb_net_priv *priv;
	unsigned int channel;
	bool report_error;

	channel = es->channel;
	if (channel >= dev->nchannels) {
		dev_err(&dev->intf->dev,
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
		kvaser_usb_leaf_rx_error(dev, es);
}

static void kvaser_usb_leaf_usbcan_rx_error(const struct kvaser_usb *dev,
					    const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_err_summary es = { };

	switch (cmd->id) {
	/* Sometimes errors are sent as unsolicited chip state events */
	case CMD_CHIP_STATE_EVENT:
		es.channel = cmd->u.usbcan.chip_state_event.channel;
		es.status = cmd->u.usbcan.chip_state_event.status;
		es.txerr = cmd->u.usbcan.chip_state_event.tx_errors_count;
		es.rxerr = cmd->u.usbcan.chip_state_event.rx_errors_count;
		kvaser_usb_leaf_usbcan_conditionally_rx_error(dev, &es);
		break;

	case CMD_CAN_ERROR_EVENT:
		es.channel = 0;
		es.status = cmd->u.usbcan.can_error_event.status_ch0;
		es.txerr = cmd->u.usbcan.can_error_event.tx_errors_count_ch0;
		es.rxerr = cmd->u.usbcan.can_error_event.rx_errors_count_ch0;
		es.usbcan.other_ch_status =
			cmd->u.usbcan.can_error_event.status_ch1;
		kvaser_usb_leaf_usbcan_conditionally_rx_error(dev, &es);

		/* The USBCAN firmware supports up to 2 channels.
		 * Now that ch0 was checked, check if ch1 has any errors.
		 */
		if (dev->nchannels == MAX_USBCAN_NET_DEVICES) {
			es.channel = 1;
			es.status = cmd->u.usbcan.can_error_event.status_ch1;
			es.txerr =
				cmd->u.usbcan.can_error_event.tx_errors_count_ch1;
			es.rxerr =
				cmd->u.usbcan.can_error_event.rx_errors_count_ch1;
			es.usbcan.other_ch_status =
				cmd->u.usbcan.can_error_event.status_ch0;
			kvaser_usb_leaf_usbcan_conditionally_rx_error(dev, &es);
		}
		break;

	default:
		dev_err(&dev->intf->dev, "Invalid cmd id (%d)\n", cmd->id);
	}
}

static void kvaser_usb_leaf_leaf_rx_error(const struct kvaser_usb *dev,
					  const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_err_summary es = { };

	switch (cmd->id) {
	case CMD_CAN_ERROR_EVENT:
		es.channel = cmd->u.leaf.can_error_event.channel;
		es.status = cmd->u.leaf.can_error_event.status;
		es.txerr = cmd->u.leaf.can_error_event.tx_errors_count;
		es.rxerr = cmd->u.leaf.can_error_event.rx_errors_count;
		es.leaf.error_factor = cmd->u.leaf.can_error_event.error_factor;
		break;
	case CMD_LEAF_LOG_MESSAGE:
		es.channel = cmd->u.leaf.log_message.channel;
		es.status = cmd->u.leaf.log_message.data[0];
		es.txerr = cmd->u.leaf.log_message.data[2];
		es.rxerr = cmd->u.leaf.log_message.data[3];
		es.leaf.error_factor = cmd->u.leaf.log_message.data[1];
		break;
	case CMD_CHIP_STATE_EVENT:
		es.channel = cmd->u.leaf.chip_state_event.channel;
		es.status = cmd->u.leaf.chip_state_event.status;
		es.txerr = cmd->u.leaf.chip_state_event.tx_errors_count;
		es.rxerr = cmd->u.leaf.chip_state_event.rx_errors_count;
		es.leaf.error_factor = 0;
		break;
	default:
		dev_err(&dev->intf->dev, "Invalid cmd id (%d)\n", cmd->id);
		return;
	}

	kvaser_usb_leaf_rx_error(dev, &es);
}

static void kvaser_usb_leaf_rx_can_err(const struct kvaser_usb_net_priv *priv,
				       const struct kvaser_cmd *cmd)
{
	if (cmd->u.rx_can_header.flag & (MSG_FLAG_ERROR_FRAME |
					 MSG_FLAG_NERR)) {
		struct net_device_stats *stats = &priv->netdev->stats;

		netdev_err(priv->netdev, "Unknown error (flags: 0x%02x)\n",
			   cmd->u.rx_can_header.flag);

		stats->rx_errors++;
		return;
	}

	if (cmd->u.rx_can_header.flag & MSG_FLAG_OVERRUN)
		kvaser_usb_can_rx_over_error(priv->netdev);
}

static void kvaser_usb_leaf_rx_can_msg(const struct kvaser_usb *dev,
				       const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv;
	struct can_frame *cf;
	struct sk_buff *skb;
	struct net_device_stats *stats;
	u8 channel = cmd->u.rx_can_header.channel;
	const u8 *rx_data = NULL;	/* GCC */

	if (channel >= dev->nchannels) {
		dev_err(&dev->intf->dev,
			"Invalid channel number (%d)\n", channel);
		return;
	}

	priv = dev->nets[channel];
	stats = &priv->netdev->stats;

	if ((cmd->u.rx_can_header.flag & MSG_FLAG_ERROR_FRAME) &&
	    (dev->driver_info->family == KVASER_LEAF &&
	     cmd->id == CMD_LEAF_LOG_MESSAGE)) {
		kvaser_usb_leaf_leaf_rx_error(dev, cmd);
		return;
	} else if (cmd->u.rx_can_header.flag & (MSG_FLAG_ERROR_FRAME |
						MSG_FLAG_NERR |
						MSG_FLAG_OVERRUN)) {
		kvaser_usb_leaf_rx_can_err(priv, cmd);
		return;
	} else if (cmd->u.rx_can_header.flag & ~MSG_FLAG_REMOTE_FRAME) {
		netdev_warn(priv->netdev,
			    "Unhandled frame (flags: 0x%02x)\n",
			    cmd->u.rx_can_header.flag);
		return;
	}

	switch (dev->driver_info->family) {
	case KVASER_LEAF:
		rx_data = cmd->u.leaf.rx_can.data;
		break;
	case KVASER_USBCAN:
		rx_data = cmd->u.usbcan.rx_can.data;
		break;
	}

	skb = alloc_can_skb(priv->netdev, &cf);
	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	if (dev->driver_info->family == KVASER_LEAF && cmd->id ==
	    CMD_LEAF_LOG_MESSAGE) {
		cf->can_id = le32_to_cpu(cmd->u.leaf.log_message.id);
		if (cf->can_id & KVASER_EXTENDED_FRAME)
			cf->can_id &= CAN_EFF_MASK | CAN_EFF_FLAG;
		else
			cf->can_id &= CAN_SFF_MASK;

		cf->len = can_cc_dlc2len(cmd->u.leaf.log_message.dlc);

		if (cmd->u.leaf.log_message.flags & MSG_FLAG_REMOTE_FRAME)
			cf->can_id |= CAN_RTR_FLAG;
		else
			memcpy(cf->data, &cmd->u.leaf.log_message.data,
			       cf->len);
	} else {
		cf->can_id = ((rx_data[0] & 0x1f) << 6) | (rx_data[1] & 0x3f);

		if (cmd->id == CMD_RX_EXT_MESSAGE) {
			cf->can_id <<= 18;
			cf->can_id |= ((rx_data[2] & 0x0f) << 14) |
				      ((rx_data[3] & 0xff) << 6) |
				      (rx_data[4] & 0x3f);
			cf->can_id |= CAN_EFF_FLAG;
		}

		cf->len = can_cc_dlc2len(rx_data[5]);

		if (cmd->u.rx_can_header.flag & MSG_FLAG_REMOTE_FRAME)
			cf->can_id |= CAN_RTR_FLAG;
		else
			memcpy(cf->data, &rx_data[6], cf->len);
	}

	stats->rx_packets++;
	if (!(cf->can_id & CAN_RTR_FLAG))
		stats->rx_bytes += cf->len;
	netif_rx(skb);
}

static void kvaser_usb_leaf_error_event_parameter(const struct kvaser_usb *dev,
						  const struct kvaser_cmd *cmd)
{
	u16 info1 = 0;

	switch (dev->driver_info->family) {
	case KVASER_LEAF:
		info1 = le16_to_cpu(cmd->u.leaf.error_event.info1);
		break;
	case KVASER_USBCAN:
		info1 = le16_to_cpu(cmd->u.usbcan.error_event.info1);
		break;
	}

	/* info1 will contain the offending cmd_no */
	switch (info1) {
	case CMD_SET_CTRL_MODE:
		dev_warn(&dev->intf->dev,
			 "CMD_SET_CTRL_MODE error in parameter\n");
		break;

	case CMD_SET_BUS_PARAMS:
		dev_warn(&dev->intf->dev,
			 "CMD_SET_BUS_PARAMS error in parameter\n");
		break;

	default:
		dev_warn(&dev->intf->dev,
			 "Unhandled parameter error event cmd_no (%u)\n",
			 info1);
		break;
	}
}

static void kvaser_usb_leaf_error_event(const struct kvaser_usb *dev,
					const struct kvaser_cmd *cmd)
{
	u8 error_code = 0;

	switch (dev->driver_info->family) {
	case KVASER_LEAF:
		error_code = cmd->u.leaf.error_event.error_code;
		break;
	case KVASER_USBCAN:
		error_code = cmd->u.usbcan.error_event.error_code;
		break;
	}

	switch (error_code) {
	case KVASER_USB_LEAF_ERROR_EVENT_TX_QUEUE_FULL:
		/* Received additional CAN message, when firmware TX queue is
		 * already full. Something is wrong with the driver.
		 * This should never happen!
		 */
		dev_err(&dev->intf->dev,
			"Received error event TX_QUEUE_FULL\n");
		break;
	case KVASER_USB_LEAF_ERROR_EVENT_PARAM:
		kvaser_usb_leaf_error_event_parameter(dev, cmd);
		break;

	default:
		dev_warn(&dev->intf->dev,
			 "Unhandled error event (%d)\n", error_code);
		break;
	}
}

static void kvaser_usb_leaf_start_chip_reply(const struct kvaser_usb *dev,
					     const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv;
	u8 channel = cmd->u.simple.channel;

	if (channel >= dev->nchannels) {
		dev_err(&dev->intf->dev,
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

static void kvaser_usb_leaf_stop_chip_reply(const struct kvaser_usb *dev,
					    const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv;
	u8 channel = cmd->u.simple.channel;

	if (channel >= dev->nchannels) {
		dev_err(&dev->intf->dev,
			"Invalid channel number (%d)\n", channel);
		return;
	}

	priv = dev->nets[channel];

	complete(&priv->stop_comp);
}

static void kvaser_usb_leaf_get_busparams_reply(const struct kvaser_usb *dev,
						const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv;
	u8 channel = cmd->u.busparams.channel;

	if (channel >= dev->nchannels) {
		dev_err(&dev->intf->dev,
			"Invalid channel number (%d)\n", channel);
		return;
	}

	priv = dev->nets[channel];
	memcpy(&priv->busparams_nominal, &cmd->u.busparams.busparams,
	       sizeof(priv->busparams_nominal));

	complete(&priv->get_busparams_comp);
}

static void kvaser_usb_leaf_handle_command(const struct kvaser_usb *dev,
					   const struct kvaser_cmd *cmd)
{
	if (kvaser_usb_leaf_verify_size(dev, cmd) < 0)
		return;

	switch (cmd->id) {
	case CMD_START_CHIP_REPLY:
		kvaser_usb_leaf_start_chip_reply(dev, cmd);
		break;

	case CMD_STOP_CHIP_REPLY:
		kvaser_usb_leaf_stop_chip_reply(dev, cmd);
		break;

	case CMD_RX_STD_MESSAGE:
	case CMD_RX_EXT_MESSAGE:
		kvaser_usb_leaf_rx_can_msg(dev, cmd);
		break;

	case CMD_LEAF_LOG_MESSAGE:
		if (dev->driver_info->family != KVASER_LEAF)
			goto warn;
		kvaser_usb_leaf_rx_can_msg(dev, cmd);
		break;

	case CMD_CHIP_STATE_EVENT:
	case CMD_CAN_ERROR_EVENT:
		if (dev->driver_info->family == KVASER_LEAF)
			kvaser_usb_leaf_leaf_rx_error(dev, cmd);
		else
			kvaser_usb_leaf_usbcan_rx_error(dev, cmd);
		break;

	case CMD_TX_ACKNOWLEDGE:
		kvaser_usb_leaf_tx_acknowledge(dev, cmd);
		break;

	case CMD_ERROR_EVENT:
		kvaser_usb_leaf_error_event(dev, cmd);
		break;

	case CMD_GET_BUS_PARAMS_REPLY:
		kvaser_usb_leaf_get_busparams_reply(dev, cmd);
		break;

	/* Ignored commands */
	case CMD_USBCAN_CLOCK_OVERFLOW_EVENT:
		if (dev->driver_info->family != KVASER_USBCAN)
			goto warn;
		break;

	case CMD_FLUSH_QUEUE_REPLY:
		if (dev->driver_info->family != KVASER_LEAF)
			goto warn;
		break;

	default:
warn:		dev_warn(&dev->intf->dev, "Unhandled command (%d)\n", cmd->id);
		break;
	}
}

static void kvaser_usb_leaf_read_bulk_callback(struct kvaser_usb *dev,
					       void *buf, int len)
{
	struct kvaser_cmd *cmd;
	int pos = 0;

	while (pos <= len - CMD_HEADER_LEN) {
		cmd = buf + pos;

		/* The Kvaser firmware can only read and write commands that
		 * does not cross the USB's endpoint wMaxPacketSize boundary.
		 * If a follow-up command crosses such boundary, firmware puts
		 * a placeholder zero-length command in its place then aligns
		 * the real command to the next max packet size.
		 *
		 * Handle such cases or we're going to miss a significant
		 * number of events in case of a heavy rx load on the bus.
		 */
		if (cmd->len == 0) {
			pos = round_up(pos, le16_to_cpu
						(dev->bulk_in->wMaxPacketSize));
			continue;
		}

		if (pos + cmd->len > len) {
			dev_err_ratelimited(&dev->intf->dev, "Format error\n");
			break;
		}

		kvaser_usb_leaf_handle_command(dev, cmd);
		pos += cmd->len;
	}
}

static int kvaser_usb_leaf_set_opt_mode(const struct kvaser_usb_net_priv *priv)
{
	struct kvaser_cmd *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->id = CMD_SET_CTRL_MODE;
	cmd->len = CMD_HEADER_LEN + sizeof(struct kvaser_cmd_ctrl_mode);
	cmd->u.ctrl_mode.tid = 0xff;
	cmd->u.ctrl_mode.channel = priv->channel;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		cmd->u.ctrl_mode.ctrl_mode = KVASER_CTRL_MODE_SILENT;
	else
		cmd->u.ctrl_mode.ctrl_mode = KVASER_CTRL_MODE_NORMAL;

	rc = kvaser_usb_send_cmd(priv->dev, cmd, cmd->len);

	kfree(cmd);
	return rc;
}

static int kvaser_usb_leaf_start_chip(struct kvaser_usb_net_priv *priv)
{
	int err;

	reinit_completion(&priv->start_comp);

	err = kvaser_usb_leaf_send_simple_cmd(priv->dev, CMD_START_CHIP,
					      priv->channel);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&priv->start_comp,
					 msecs_to_jiffies(KVASER_USB_TIMEOUT)))
		return -ETIMEDOUT;

	return 0;
}

static int kvaser_usb_leaf_stop_chip(struct kvaser_usb_net_priv *priv)
{
	struct kvaser_usb_net_leaf_priv *leaf = priv->sub_priv;
	int err;

	reinit_completion(&priv->stop_comp);

	cancel_delayed_work(&leaf->chip_state_req_work);

	err = kvaser_usb_leaf_send_simple_cmd(priv->dev, CMD_STOP_CHIP,
					      priv->channel);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&priv->stop_comp,
					 msecs_to_jiffies(KVASER_USB_TIMEOUT)))
		return -ETIMEDOUT;

	return 0;
}

static int kvaser_usb_leaf_reset_chip(struct kvaser_usb *dev, int channel)
{
	return kvaser_usb_leaf_send_simple_cmd(dev, CMD_RESET_CHIP, channel);
}

static int kvaser_usb_leaf_flush_queue(struct kvaser_usb_net_priv *priv)
{
	struct kvaser_cmd *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->id = CMD_FLUSH_QUEUE;
	cmd->len = CMD_HEADER_LEN + sizeof(struct kvaser_cmd_flush_queue);
	cmd->u.flush_queue.channel = priv->channel;
	cmd->u.flush_queue.flags = 0x00;

	rc = kvaser_usb_send_cmd(priv->dev, cmd, cmd->len);

	kfree(cmd);
	return rc;
}

static int kvaser_usb_leaf_init_card(struct kvaser_usb *dev)
{
	struct kvaser_usb_dev_card_data *card_data = &dev->card_data;

	card_data->ctrlmode_supported |= CAN_CTRLMODE_3_SAMPLES;

	return 0;
}

static int kvaser_usb_leaf_init_channel(struct kvaser_usb_net_priv *priv)
{
	struct kvaser_usb_net_leaf_priv *leaf;

	leaf = devm_kzalloc(&priv->dev->intf->dev, sizeof(*leaf), GFP_KERNEL);
	if (!leaf)
		return -ENOMEM;

	leaf->net = priv;
	INIT_DELAYED_WORK(&leaf->chip_state_req_work,
			  kvaser_usb_leaf_chip_state_req_work);

	priv->sub_priv = leaf;

	return 0;
}

static void kvaser_usb_leaf_remove_channel(struct kvaser_usb_net_priv *priv)
{
	struct kvaser_usb_net_leaf_priv *leaf = priv->sub_priv;

	if (leaf)
		cancel_delayed_work_sync(&leaf->chip_state_req_work);
}

static int kvaser_usb_leaf_set_bittiming(const struct net_device *netdev,
					 const struct kvaser_usb_busparams *busparams)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct kvaser_usb *dev = priv->dev;
	struct kvaser_cmd *cmd;
	int rc;

	cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->id = CMD_SET_BUS_PARAMS;
	cmd->len = CMD_HEADER_LEN + sizeof(struct kvaser_cmd_busparams);
	cmd->u.busparams.channel = priv->channel;
	cmd->u.busparams.tid = 0xff;
	memcpy(&cmd->u.busparams.busparams, busparams,
	       sizeof(cmd->u.busparams.busparams));

	rc = kvaser_usb_send_cmd(dev, cmd, cmd->len);

	kfree(cmd);
	return rc;
}

static int kvaser_usb_leaf_get_busparams(struct kvaser_usb_net_priv *priv)
{
	int err;

	if (priv->dev->driver_info->family == KVASER_USBCAN)
		return -EOPNOTSUPP;

	reinit_completion(&priv->get_busparams_comp);

	err = kvaser_usb_leaf_send_simple_cmd(priv->dev, CMD_GET_BUS_PARAMS,
					      priv->channel);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&priv->get_busparams_comp,
					 msecs_to_jiffies(KVASER_USB_TIMEOUT)))
		return -ETIMEDOUT;

	return 0;
}

static int kvaser_usb_leaf_set_mode(struct net_device *netdev,
				    enum can_mode mode)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	int err;

	switch (mode) {
	case CAN_MODE_START:
		kvaser_usb_unlink_tx_urbs(priv);

		err = kvaser_usb_leaf_simple_cmd_async(priv, CMD_START_CHIP);
		if (err)
			return err;

		priv->can.state = CAN_STATE_ERROR_ACTIVE;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int kvaser_usb_leaf_get_berr_counter(const struct net_device *netdev,
					    struct can_berr_counter *bec)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);

	*bec = priv->bec;

	return 0;
}

static int kvaser_usb_leaf_setup_endpoints(struct kvaser_usb *dev)
{
	const struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i;

	iface_desc = dev->intf->cur_altsetting;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in && usb_endpoint_is_bulk_in(endpoint))
			dev->bulk_in = endpoint;

		if (!dev->bulk_out && usb_endpoint_is_bulk_out(endpoint))
			dev->bulk_out = endpoint;

		/* use first bulk endpoint for in and out */
		if (dev->bulk_in && dev->bulk_out)
			return 0;
	}

	return -ENODEV;
}

const struct kvaser_usb_dev_ops kvaser_usb_leaf_dev_ops = {
	.dev_set_mode = kvaser_usb_leaf_set_mode,
	.dev_set_bittiming = kvaser_usb_leaf_set_bittiming,
	.dev_get_busparams = kvaser_usb_leaf_get_busparams,
	.dev_set_data_bittiming = NULL,
	.dev_get_data_busparams = NULL,
	.dev_get_berr_counter = kvaser_usb_leaf_get_berr_counter,
	.dev_setup_endpoints = kvaser_usb_leaf_setup_endpoints,
	.dev_init_card = kvaser_usb_leaf_init_card,
	.dev_init_channel = kvaser_usb_leaf_init_channel,
	.dev_remove_channel = kvaser_usb_leaf_remove_channel,
	.dev_get_software_info = kvaser_usb_leaf_get_software_info,
	.dev_get_software_details = NULL,
	.dev_get_card_info = kvaser_usb_leaf_get_card_info,
	.dev_get_capabilities = kvaser_usb_leaf_get_capabilities,
	.dev_set_opt_mode = kvaser_usb_leaf_set_opt_mode,
	.dev_start_chip = kvaser_usb_leaf_start_chip,
	.dev_stop_chip = kvaser_usb_leaf_stop_chip,
	.dev_reset_chip = kvaser_usb_leaf_reset_chip,
	.dev_flush_queue = kvaser_usb_leaf_flush_queue,
	.dev_read_bulk_callback = kvaser_usb_leaf_read_bulk_callback,
	.dev_frame_to_cmd = kvaser_usb_leaf_frame_to_cmd,
};
