// SPDX-License-Identifier: GPL-2.0
/* Parts of this driver are based on the following:
 *  - Kvaser linux mhydra driver (version 5.24)
 *  - CAN driver for esd CAN-USB/2
 *
 * Copyright (C) 2018 KVASER AB, Sweden. All rights reserved.
 * Copyright (C) 2010 Matthias Fuchs <matthias.fuchs@esd.eu>, esd gmbh
 *
 * Known issues:
 *  - Transition from CAN_STATE_ERROR_WARNING to CAN_STATE_ERROR_ACTIVE is only
 *    reported after a call to do_get_berr_counter(), since firmware does not
 *    distinguish between ERROR_WARNING and ERROR_ACTIVE.
 *  - Hardware timestamps are not set for CAN Tx frames.
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

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/netlink.h>

#include "kvaser_usb.h"

/* Forward declarations */
static const struct kvaser_usb_dev_cfg kvaser_usb_hydra_dev_cfg_kcan;
static const struct kvaser_usb_dev_cfg kvaser_usb_hydra_dev_cfg_flexc;
static const struct kvaser_usb_dev_cfg kvaser_usb_hydra_dev_cfg_rt;

#define KVASER_USB_HYDRA_BULK_EP_IN_ADDR	0x82
#define KVASER_USB_HYDRA_BULK_EP_OUT_ADDR	0x02

#define KVASER_USB_HYDRA_MAX_TRANSID		0xff
#define KVASER_USB_HYDRA_MIN_TRANSID		0x01

/* Minihydra command IDs */
#define CMD_SET_BUSPARAMS_REQ			16
#define CMD_GET_CHIP_STATE_REQ			19
#define CMD_CHIP_STATE_EVENT			20
#define CMD_SET_DRIVERMODE_REQ			21
#define CMD_START_CHIP_REQ			26
#define CMD_START_CHIP_RESP			27
#define CMD_STOP_CHIP_REQ			28
#define CMD_STOP_CHIP_RESP			29
#define CMD_TX_CAN_MESSAGE			33
#define CMD_GET_CARD_INFO_REQ			34
#define CMD_GET_CARD_INFO_RESP			35
#define CMD_GET_SOFTWARE_INFO_REQ		38
#define CMD_GET_SOFTWARE_INFO_RESP		39
#define CMD_ERROR_EVENT				45
#define CMD_FLUSH_QUEUE				48
#define CMD_TX_ACKNOWLEDGE			50
#define CMD_FLUSH_QUEUE_RESP			66
#define CMD_SET_BUSPARAMS_FD_REQ		69
#define CMD_SET_BUSPARAMS_FD_RESP		70
#define CMD_SET_BUSPARAMS_RESP			85
#define CMD_GET_CAPABILITIES_REQ		95
#define CMD_GET_CAPABILITIES_RESP		96
#define CMD_RX_MESSAGE				106
#define CMD_MAP_CHANNEL_REQ			200
#define CMD_MAP_CHANNEL_RESP			201
#define CMD_GET_SOFTWARE_DETAILS_REQ		202
#define CMD_GET_SOFTWARE_DETAILS_RESP		203
#define CMD_EXTENDED				255

/* Minihydra extended command IDs */
#define CMD_TX_CAN_MESSAGE_FD			224
#define CMD_TX_ACKNOWLEDGE_FD			225
#define CMD_RX_MESSAGE_FD			226

/* Hydra commands are handled by different threads in firmware.
 * The threads are denoted hydra entity (HE). Each HE got a unique 6-bit
 * address. The address is used in hydra commands to get/set source and
 * destination HE. There are two predefined HE addresses, the remaining
 * addresses are different between devices and firmware versions. Hence, we need
 * to enumerate the addresses (see kvaser_usb_hydra_map_channel()).
 */

/* Well-known HE addresses */
#define KVASER_USB_HYDRA_HE_ADDRESS_ROUTER	0x00
#define KVASER_USB_HYDRA_HE_ADDRESS_ILLEGAL	0x3e

#define KVASER_USB_HYDRA_TRANSID_CANHE		0x40
#define KVASER_USB_HYDRA_TRANSID_SYSDBG		0x61

struct kvaser_cmd_map_ch_req {
	char name[16];
	u8 channel;
	u8 reserved[11];
} __packed;

struct kvaser_cmd_map_ch_res {
	u8 he_addr;
	u8 channel;
	u8 reserved[26];
} __packed;

struct kvaser_cmd_card_info {
	__le32 serial_number;
	__le32 clock_res;
	__le32 mfg_date;
	__le32 ean[2];
	u8 hw_version;
	u8 usb_mode;
	u8 hw_type;
	u8 reserved0;
	u8 nchannels;
	u8 reserved1[3];
} __packed;

struct kvaser_cmd_sw_info {
	u8 reserved0[8];
	__le16 max_outstanding_tx;
	u8 reserved1[18];
} __packed;

struct kvaser_cmd_sw_detail_req {
	u8 use_ext_cmd;
	u8 reserved[27];
} __packed;

/* Software detail flags */
#define KVASER_USB_HYDRA_SW_FLAG_FW_BETA	BIT(2)
#define KVASER_USB_HYDRA_SW_FLAG_FW_BAD		BIT(4)
#define KVASER_USB_HYDRA_SW_FLAG_FREQ_80M	BIT(5)
#define KVASER_USB_HYDRA_SW_FLAG_EXT_CMD	BIT(9)
#define KVASER_USB_HYDRA_SW_FLAG_CANFD		BIT(10)
#define KVASER_USB_HYDRA_SW_FLAG_NONISO		BIT(11)
#define KVASER_USB_HYDRA_SW_FLAG_EXT_CAP	BIT(12)
#define KVASER_USB_HYDRA_SW_FLAG_CAN_FREQ_80M	BIT(13)
struct kvaser_cmd_sw_detail_res {
	__le32 sw_flags;
	__le32 sw_version;
	__le32 sw_name;
	__le32 ean[2];
	__le32 max_bitrate;
	u8 reserved[4];
} __packed;

/* Sub commands for cap_req and cap_res */
#define KVASER_USB_HYDRA_CAP_CMD_LISTEN_MODE	0x02
#define KVASER_USB_HYDRA_CAP_CMD_ERR_REPORT	0x05
#define KVASER_USB_HYDRA_CAP_CMD_ONE_SHOT	0x06
struct kvaser_cmd_cap_req {
	__le16 cap_cmd;
	u8 reserved[26];
} __packed;

/* Status codes for cap_res */
#define KVASER_USB_HYDRA_CAP_STAT_OK		0x00
#define KVASER_USB_HYDRA_CAP_STAT_NOT_IMPL	0x01
#define KVASER_USB_HYDRA_CAP_STAT_UNAVAIL	0x02
struct kvaser_cmd_cap_res {
	__le16 cap_cmd;
	__le16 status;
	__le32 mask;
	__le32 value;
	u8 reserved[16];
} __packed;

/* CMD_ERROR_EVENT error codes */
#define KVASER_USB_HYDRA_ERROR_EVENT_CAN	0x01
#define KVASER_USB_HYDRA_ERROR_EVENT_PARAM	0x09
struct kvaser_cmd_error_event {
	__le16 timestamp[3];
	u8 reserved;
	u8 error_code;
	__le16 info1;
	__le16 info2;
} __packed;

/* Chip state status flags. Used for chip_state_event and err_frame_data. */
#define KVASER_USB_HYDRA_BUS_ERR_ACT		0x00
#define KVASER_USB_HYDRA_BUS_ERR_PASS		BIT(5)
#define KVASER_USB_HYDRA_BUS_BUS_OFF		BIT(6)
struct kvaser_cmd_chip_state_event {
	__le16 timestamp[3];
	u8 tx_err_counter;
	u8 rx_err_counter;
	u8 bus_status;
	u8 reserved[19];
} __packed;

/* Busparam modes */
#define KVASER_USB_HYDRA_BUS_MODE_CAN		0x00
#define KVASER_USB_HYDRA_BUS_MODE_CANFD_ISO	0x01
#define KVASER_USB_HYDRA_BUS_MODE_NONISO	0x02
struct kvaser_cmd_set_busparams {
	__le32 bitrate;
	u8 tseg1;
	u8 tseg2;
	u8 sjw;
	u8 nsamples;
	u8 reserved0[4];
	__le32 bitrate_d;
	u8 tseg1_d;
	u8 tseg2_d;
	u8 sjw_d;
	u8 nsamples_d;
	u8 canfd_mode;
	u8 reserved1[7];
} __packed;

/* Ctrl modes */
#define KVASER_USB_HYDRA_CTRLMODE_NORMAL	0x01
#define KVASER_USB_HYDRA_CTRLMODE_LISTEN	0x02
struct kvaser_cmd_set_ctrlmode {
	u8 mode;
	u8 reserved[27];
} __packed;

struct kvaser_err_frame_data {
	u8 bus_status;
	u8 reserved0;
	u8 tx_err_counter;
	u8 rx_err_counter;
	u8 reserved1[4];
} __packed;

struct kvaser_cmd_rx_can {
	u8 cmd_len;
	u8 cmd_no;
	u8 channel;
	u8 flags;
	__le16 timestamp[3];
	u8 dlc;
	u8 padding;
	__le32 id;
	union {
		u8 data[8];
		struct kvaser_err_frame_data err_frame_data;
	};
} __packed;

/* Extended CAN ID flag. Used in rx_can and tx_can */
#define KVASER_USB_HYDRA_EXTENDED_FRAME_ID	BIT(31)
struct kvaser_cmd_tx_can {
	__le32 id;
	u8 data[8];
	u8 dlc;
	u8 flags;
	__le16 transid;
	u8 channel;
	u8 reserved[11];
} __packed;

struct kvaser_cmd_header {
	u8 cmd_no;
	/* The destination HE address is stored in 0..5 of he_addr.
	 * The upper part of source HE address is stored in 6..7 of he_addr, and
	 * the lower part is stored in 12..15 of transid.
	 */
	u8 he_addr;
	__le16 transid;
} __packed;

struct kvaser_cmd {
	struct kvaser_cmd_header header;
	union {
		struct kvaser_cmd_map_ch_req map_ch_req;
		struct kvaser_cmd_map_ch_res map_ch_res;

		struct kvaser_cmd_card_info card_info;
		struct kvaser_cmd_sw_info sw_info;
		struct kvaser_cmd_sw_detail_req sw_detail_req;
		struct kvaser_cmd_sw_detail_res sw_detail_res;

		struct kvaser_cmd_cap_req cap_req;
		struct kvaser_cmd_cap_res cap_res;

		struct kvaser_cmd_error_event error_event;

		struct kvaser_cmd_set_busparams set_busparams_req;

		struct kvaser_cmd_chip_state_event chip_state_event;

		struct kvaser_cmd_set_ctrlmode set_ctrlmode;

		struct kvaser_cmd_rx_can rx_can;
		struct kvaser_cmd_tx_can tx_can;
	} __packed;
} __packed;

/* CAN frame flags. Used in rx_can, ext_rx_can, tx_can and ext_tx_can */
#define KVASER_USB_HYDRA_CF_FLAG_ERROR_FRAME	BIT(0)
#define KVASER_USB_HYDRA_CF_FLAG_OVERRUN	BIT(1)
#define KVASER_USB_HYDRA_CF_FLAG_REMOTE_FRAME	BIT(4)
#define KVASER_USB_HYDRA_CF_FLAG_EXTENDED_ID	BIT(5)
#define KVASER_USB_HYDRA_CF_FLAG_TX_ACK		BIT(6)
/* CAN frame flags. Used in ext_rx_can and ext_tx_can */
#define KVASER_USB_HYDRA_CF_FLAG_OSM_NACK	BIT(12)
#define KVASER_USB_HYDRA_CF_FLAG_ABL		BIT(13)
#define KVASER_USB_HYDRA_CF_FLAG_FDF		BIT(16)
#define KVASER_USB_HYDRA_CF_FLAG_BRS		BIT(17)
#define KVASER_USB_HYDRA_CF_FLAG_ESI		BIT(18)

/* KCAN packet header macros. Used in ext_rx_can and ext_tx_can */
#define KVASER_USB_KCAN_DATA_DLC_BITS		4
#define KVASER_USB_KCAN_DATA_DLC_SHIFT		8
#define KVASER_USB_KCAN_DATA_DLC_MASK \
				GENMASK(KVASER_USB_KCAN_DATA_DLC_BITS - 1 + \
				KVASER_USB_KCAN_DATA_DLC_SHIFT, \
				KVASER_USB_KCAN_DATA_DLC_SHIFT)

#define KVASER_USB_KCAN_DATA_BRS		BIT(14)
#define KVASER_USB_KCAN_DATA_FDF		BIT(15)
#define KVASER_USB_KCAN_DATA_OSM		BIT(16)
#define KVASER_USB_KCAN_DATA_AREQ		BIT(31)
#define KVASER_USB_KCAN_DATA_SRR		BIT(31)
#define KVASER_USB_KCAN_DATA_RTR		BIT(29)
#define KVASER_USB_KCAN_DATA_IDE		BIT(30)
struct kvaser_cmd_ext_rx_can {
	__le32 flags;
	__le32 id;
	__le32 kcan_id;
	__le32 kcan_header;
	__le64 timestamp;
	union {
		u8 kcan_payload[64];
		struct kvaser_err_frame_data err_frame_data;
	};
} __packed;

struct kvaser_cmd_ext_tx_can {
	__le32 flags;
	__le32 id;
	__le32 kcan_id;
	__le32 kcan_header;
	u8 databytes;
	u8 dlc;
	u8 reserved[6];
	u8 kcan_payload[64];
} __packed;

struct kvaser_cmd_ext_tx_ack {
	__le32 flags;
	u8 reserved0[4];
	__le64 timestamp;
	u8 reserved1[8];
} __packed;

/* struct for extended commands (CMD_EXTENDED) */
struct kvaser_cmd_ext {
	struct kvaser_cmd_header header;
	__le16 len;
	u8 cmd_no_ext;
	u8 reserved;

	union {
		struct kvaser_cmd_ext_rx_can rx_can;
		struct kvaser_cmd_ext_tx_can tx_can;
		struct kvaser_cmd_ext_tx_ack tx_ack;
	} __packed;
} __packed;

static const struct can_bittiming_const kvaser_usb_hydra_kcan_bittiming_c = {
	.name = "kvaser_usb_kcan",
	.tseg1_min = 1,
	.tseg1_max = 255,
	.tseg2_min = 1,
	.tseg2_max = 32,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 8192,
	.brp_inc = 1,
};

static const struct can_bittiming_const kvaser_usb_hydra_flexc_bittiming_c = {
	.name = "kvaser_usb_flex",
	.tseg1_min = 4,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

static const struct can_bittiming_const kvaser_usb_hydra_rt_bittiming_c = {
	.name = "kvaser_usb_rt",
	.tseg1_min = 2,
	.tseg1_max = 96,
	.tseg2_min = 2,
	.tseg2_max = 32,
	.sjw_max = 32,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

static const struct can_bittiming_const kvaser_usb_hydra_rtd_bittiming_c = {
	.name = "kvaser_usb_rt",
	.tseg1_min = 2,
	.tseg1_max = 39,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 8,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

#define KVASER_USB_HYDRA_TRANSID_BITS		12
#define KVASER_USB_HYDRA_TRANSID_MASK \
				GENMASK(KVASER_USB_HYDRA_TRANSID_BITS - 1, 0)
#define KVASER_USB_HYDRA_HE_ADDR_SRC_MASK	GENMASK(7, 6)
#define KVASER_USB_HYDRA_HE_ADDR_DEST_MASK	GENMASK(5, 0)
#define KVASER_USB_HYDRA_HE_ADDR_SRC_BITS	2
static inline u16 kvaser_usb_hydra_get_cmd_transid(const struct kvaser_cmd *cmd)
{
	return le16_to_cpu(cmd->header.transid) & KVASER_USB_HYDRA_TRANSID_MASK;
}

static inline void kvaser_usb_hydra_set_cmd_transid(struct kvaser_cmd *cmd,
						    u16 transid)
{
	cmd->header.transid =
			cpu_to_le16(transid & KVASER_USB_HYDRA_TRANSID_MASK);
}

static inline u8 kvaser_usb_hydra_get_cmd_src_he(const struct kvaser_cmd *cmd)
{
	return (cmd->header.he_addr & KVASER_USB_HYDRA_HE_ADDR_SRC_MASK) >>
		KVASER_USB_HYDRA_HE_ADDR_SRC_BITS |
		le16_to_cpu(cmd->header.transid) >>
		KVASER_USB_HYDRA_TRANSID_BITS;
}

static inline void kvaser_usb_hydra_set_cmd_dest_he(struct kvaser_cmd *cmd,
						    u8 dest_he)
{
	cmd->header.he_addr =
		(cmd->header.he_addr & KVASER_USB_HYDRA_HE_ADDR_SRC_MASK) |
		(dest_he & KVASER_USB_HYDRA_HE_ADDR_DEST_MASK);
}

static u8 kvaser_usb_hydra_channel_from_cmd(const struct kvaser_usb *dev,
					    const struct kvaser_cmd *cmd)
{
	int i;
	u8 channel = 0xff;
	u8 src_he = kvaser_usb_hydra_get_cmd_src_he(cmd);

	for (i = 0; i < KVASER_USB_MAX_NET_DEVICES; i++) {
		if (dev->card_data.hydra.channel_to_he[i] == src_he) {
			channel = i;
			break;
		}
	}

	return channel;
}

static u16 kvaser_usb_hydra_get_next_transid(struct kvaser_usb *dev)
{
	unsigned long flags;
	u16 transid;
	struct kvaser_usb_dev_card_data_hydra *card_data =
							&dev->card_data.hydra;

	spin_lock_irqsave(&card_data->transid_lock, flags);
	transid = card_data->transid;
	if (transid >= KVASER_USB_HYDRA_MAX_TRANSID)
		transid = KVASER_USB_HYDRA_MIN_TRANSID;
	else
		transid++;
	card_data->transid = transid;
	spin_unlock_irqrestore(&card_data->transid_lock, flags);

	return transid;
}

static size_t kvaser_usb_hydra_cmd_size(struct kvaser_cmd *cmd)
{
	size_t ret;

	if (cmd->header.cmd_no == CMD_EXTENDED)
		ret = le16_to_cpu(((struct kvaser_cmd_ext *)cmd)->len);
	else
		ret = sizeof(struct kvaser_cmd);

	return ret;
}

static struct kvaser_usb_net_priv *
kvaser_usb_hydra_net_priv_from_cmd(const struct kvaser_usb *dev,
				   const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv = NULL;
	u8 channel = kvaser_usb_hydra_channel_from_cmd(dev, cmd);

	if (channel >= dev->nchannels)
		dev_err(&dev->intf->dev,
			"Invalid channel number (%d)\n", channel);
	else
		priv = dev->nets[channel];

	return priv;
}

static ktime_t
kvaser_usb_hydra_ktime_from_rx_cmd(const struct kvaser_usb_dev_cfg *cfg,
				   const struct kvaser_cmd *cmd)
{
	u64 ticks;

	if (cmd->header.cmd_no == CMD_EXTENDED) {
		struct kvaser_cmd_ext *cmd_ext = (struct kvaser_cmd_ext *)cmd;

		ticks = le64_to_cpu(cmd_ext->rx_can.timestamp);
	} else {
		ticks = le16_to_cpu(cmd->rx_can.timestamp[0]);
		ticks += (u64)(le16_to_cpu(cmd->rx_can.timestamp[1])) << 16;
		ticks += (u64)(le16_to_cpu(cmd->rx_can.timestamp[2])) << 32;
	}

	return ns_to_ktime(div_u64(ticks * 1000, cfg->timestamp_freq));
}

static int kvaser_usb_hydra_send_simple_cmd(struct kvaser_usb *dev,
					    u8 cmd_no, int channel)
{
	struct kvaser_cmd *cmd;
	int err;

	cmd = kcalloc(1, sizeof(struct kvaser_cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->header.cmd_no = cmd_no;
	if (channel < 0) {
		kvaser_usb_hydra_set_cmd_dest_he
				(cmd, KVASER_USB_HYDRA_HE_ADDRESS_ILLEGAL);
	} else {
		if (channel >= KVASER_USB_MAX_NET_DEVICES) {
			dev_err(&dev->intf->dev, "channel (%d) out of range.\n",
				channel);
			err = -EINVAL;
			goto end;
		}
		kvaser_usb_hydra_set_cmd_dest_he
			(cmd, dev->card_data.hydra.channel_to_he[channel]);
	}
	kvaser_usb_hydra_set_cmd_transid
				(cmd, kvaser_usb_hydra_get_next_transid(dev));

	err = kvaser_usb_send_cmd(dev, cmd, kvaser_usb_hydra_cmd_size(cmd));
	if (err)
		goto end;

end:
	kfree(cmd);

	return err;
}

static int
kvaser_usb_hydra_send_simple_cmd_async(struct kvaser_usb_net_priv *priv,
				       u8 cmd_no)
{
	struct kvaser_cmd *cmd;
	struct kvaser_usb *dev = priv->dev;
	int err;

	cmd = kcalloc(1, sizeof(struct kvaser_cmd), GFP_ATOMIC);
	if (!cmd)
		return -ENOMEM;

	cmd->header.cmd_no = cmd_no;

	kvaser_usb_hydra_set_cmd_dest_he
		(cmd, dev->card_data.hydra.channel_to_he[priv->channel]);
	kvaser_usb_hydra_set_cmd_transid
				(cmd, kvaser_usb_hydra_get_next_transid(dev));

	err = kvaser_usb_send_cmd_async(priv, cmd,
					kvaser_usb_hydra_cmd_size(cmd));
	if (err)
		kfree(cmd);

	return err;
}

/* This function is used for synchronously waiting on hydra control commands.
 * Note: Compared to kvaser_usb_hydra_read_bulk_callback(), we never need to
 *       handle partial hydra commands. Since hydra control commands are always
 *       non-extended commands.
 */
static int kvaser_usb_hydra_wait_cmd(const struct kvaser_usb *dev, u8 cmd_no,
				     struct kvaser_cmd *cmd)
{
	void *buf;
	int err;
	unsigned long timeout = jiffies + msecs_to_jiffies(KVASER_USB_TIMEOUT);

	if (cmd->header.cmd_no == CMD_EXTENDED) {
		dev_err(&dev->intf->dev, "Wait for CMD_EXTENDED not allowed\n");
		return -EINVAL;
	}

	buf = kzalloc(KVASER_USB_RX_BUFFER_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	do {
		int actual_len = 0;
		int pos = 0;

		err = kvaser_usb_recv_cmd(dev, buf, KVASER_USB_RX_BUFFER_SIZE,
					  &actual_len);
		if (err < 0)
			goto end;

		while (pos < actual_len) {
			struct kvaser_cmd *tmp_cmd;
			size_t cmd_len;

			tmp_cmd = buf + pos;
			cmd_len = kvaser_usb_hydra_cmd_size(tmp_cmd);
			if (pos + cmd_len > actual_len) {
				dev_err_ratelimited(&dev->intf->dev,
						    "Format error\n");
				break;
			}

			if (tmp_cmd->header.cmd_no == cmd_no) {
				memcpy(cmd, tmp_cmd, cmd_len);
				goto end;
			}
			pos += cmd_len;
		}
	} while (time_before(jiffies, timeout));

	err = -EINVAL;

end:
	kfree(buf);

	return err;
}

static int kvaser_usb_hydra_map_channel_resp(struct kvaser_usb *dev,
					     const struct kvaser_cmd *cmd)
{
	u8 he, channel;
	u16 transid = kvaser_usb_hydra_get_cmd_transid(cmd);
	struct kvaser_usb_dev_card_data_hydra *card_data =
							&dev->card_data.hydra;

	if (transid > 0x007f || transid < 0x0040) {
		dev_err(&dev->intf->dev,
			"CMD_MAP_CHANNEL_RESP, invalid transid: 0x%x\n",
			transid);
		return -EINVAL;
	}

	switch (transid) {
	case KVASER_USB_HYDRA_TRANSID_CANHE:
	case KVASER_USB_HYDRA_TRANSID_CANHE + 1:
	case KVASER_USB_HYDRA_TRANSID_CANHE + 2:
	case KVASER_USB_HYDRA_TRANSID_CANHE + 3:
	case KVASER_USB_HYDRA_TRANSID_CANHE + 4:
		channel = transid & 0x000f;
		he = cmd->map_ch_res.he_addr;
		card_data->channel_to_he[channel] = he;
		break;
	case KVASER_USB_HYDRA_TRANSID_SYSDBG:
		card_data->sysdbg_he = cmd->map_ch_res.he_addr;
		break;
	default:
		dev_warn(&dev->intf->dev,
			 "Unknown CMD_MAP_CHANNEL_RESP transid=0x%x\n",
			 transid);
		break;
	}

	return 0;
}

static int kvaser_usb_hydra_map_channel(struct kvaser_usb *dev, u16 transid,
					u8 channel, const char *name)
{
	struct kvaser_cmd *cmd;
	int err;

	cmd = kcalloc(1, sizeof(struct kvaser_cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	strcpy(cmd->map_ch_req.name, name);
	cmd->header.cmd_no = CMD_MAP_CHANNEL_REQ;
	kvaser_usb_hydra_set_cmd_dest_he
				(cmd, KVASER_USB_HYDRA_HE_ADDRESS_ROUTER);
	cmd->map_ch_req.channel = channel;

	kvaser_usb_hydra_set_cmd_transid(cmd, transid);

	err = kvaser_usb_send_cmd(dev, cmd, kvaser_usb_hydra_cmd_size(cmd));
	if (err)
		goto end;

	err = kvaser_usb_hydra_wait_cmd(dev, CMD_MAP_CHANNEL_RESP, cmd);
	if (err)
		goto end;

	err = kvaser_usb_hydra_map_channel_resp(dev, cmd);
	if (err)
		goto end;

end:
	kfree(cmd);

	return err;
}

static int kvaser_usb_hydra_get_single_capability(struct kvaser_usb *dev,
						  u16 cap_cmd_req, u16 *status)
{
	struct kvaser_usb_dev_card_data *card_data = &dev->card_data;
	struct kvaser_cmd *cmd;
	u32 value = 0;
	u32 mask = 0;
	u16 cap_cmd_res;
	int err;
	int i;

	cmd = kcalloc(1, sizeof(struct kvaser_cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->header.cmd_no = CMD_GET_CAPABILITIES_REQ;
	cmd->cap_req.cap_cmd = cpu_to_le16(cap_cmd_req);

	kvaser_usb_hydra_set_cmd_dest_he(cmd, card_data->hydra.sysdbg_he);
	kvaser_usb_hydra_set_cmd_transid
				(cmd, kvaser_usb_hydra_get_next_transid(dev));

	err = kvaser_usb_send_cmd(dev, cmd, kvaser_usb_hydra_cmd_size(cmd));
	if (err)
		goto end;

	err = kvaser_usb_hydra_wait_cmd(dev, CMD_GET_CAPABILITIES_RESP, cmd);
	if (err)
		goto end;

	*status = le16_to_cpu(cmd->cap_res.status);

	if (*status != KVASER_USB_HYDRA_CAP_STAT_OK)
		goto end;

	cap_cmd_res = le16_to_cpu(cmd->cap_res.cap_cmd);
	switch (cap_cmd_res) {
	case KVASER_USB_HYDRA_CAP_CMD_LISTEN_MODE:
	case KVASER_USB_HYDRA_CAP_CMD_ERR_REPORT:
	case KVASER_USB_HYDRA_CAP_CMD_ONE_SHOT:
		value = le32_to_cpu(cmd->cap_res.value);
		mask = le32_to_cpu(cmd->cap_res.mask);
		break;
	default:
		dev_warn(&dev->intf->dev, "Unknown capability command %u\n",
			 cap_cmd_res);
		break;
	}

	for (i = 0; i < dev->nchannels; i++) {
		if (BIT(i) & (value & mask)) {
			switch (cap_cmd_res) {
			case KVASER_USB_HYDRA_CAP_CMD_LISTEN_MODE:
				card_data->ctrlmode_supported |=
						CAN_CTRLMODE_LISTENONLY;
				break;
			case KVASER_USB_HYDRA_CAP_CMD_ERR_REPORT:
				card_data->capabilities |=
						KVASER_USB_CAP_BERR_CAP;
				break;
			case KVASER_USB_HYDRA_CAP_CMD_ONE_SHOT:
				card_data->ctrlmode_supported |=
						CAN_CTRLMODE_ONE_SHOT;
				break;
			}
		}
	}

end:
	kfree(cmd);

	return err;
}

static void kvaser_usb_hydra_start_chip_reply(const struct kvaser_usb *dev,
					      const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv;

	priv = kvaser_usb_hydra_net_priv_from_cmd(dev, cmd);
	if (!priv)
		return;

	if (completion_done(&priv->start_comp) &&
	    netif_queue_stopped(priv->netdev)) {
		netif_wake_queue(priv->netdev);
	} else {
		netif_start_queue(priv->netdev);
		complete(&priv->start_comp);
	}
}

static void kvaser_usb_hydra_stop_chip_reply(const struct kvaser_usb *dev,
					     const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv;

	priv = kvaser_usb_hydra_net_priv_from_cmd(dev, cmd);
	if (!priv)
		return;

	complete(&priv->stop_comp);
}

static void kvaser_usb_hydra_flush_queue_reply(const struct kvaser_usb *dev,
					       const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv;

	priv = kvaser_usb_hydra_net_priv_from_cmd(dev, cmd);
	if (!priv)
		return;

	complete(&priv->flush_comp);
}

static void
kvaser_usb_hydra_bus_status_to_can_state(const struct kvaser_usb_net_priv *priv,
					 u8 bus_status,
					 const struct can_berr_counter *bec,
					 enum can_state *new_state)
{
	if (bus_status & KVASER_USB_HYDRA_BUS_BUS_OFF) {
		*new_state = CAN_STATE_BUS_OFF;
	} else if (bus_status & KVASER_USB_HYDRA_BUS_ERR_PASS) {
		*new_state = CAN_STATE_ERROR_PASSIVE;
	} else if (bus_status == KVASER_USB_HYDRA_BUS_ERR_ACT) {
		if (bec->txerr >= 128 || bec->rxerr >= 128) {
			netdev_warn(priv->netdev,
				    "ERR_ACTIVE but err tx=%u or rx=%u >=128\n",
				    bec->txerr, bec->rxerr);
			*new_state = CAN_STATE_ERROR_PASSIVE;
		} else if (bec->txerr >= 96 || bec->rxerr >= 96) {
			*new_state = CAN_STATE_ERROR_WARNING;
		} else {
			*new_state = CAN_STATE_ERROR_ACTIVE;
		}
	}
}

static void kvaser_usb_hydra_update_state(struct kvaser_usb_net_priv *priv,
					  u8 bus_status,
					  const struct can_berr_counter *bec)
{
	struct net_device *netdev = priv->netdev;
	struct can_frame *cf;
	struct sk_buff *skb;
	enum can_state new_state, old_state;

	old_state = priv->can.state;

	kvaser_usb_hydra_bus_status_to_can_state(priv, bus_status, bec,
						 &new_state);

	if (new_state == old_state)
		return;

	/* Ignore state change if previous state was STOPPED and the new state
	 * is BUS_OFF. Firmware always report this as BUS_OFF, since firmware
	 * does not distinguish between BUS_OFF and STOPPED.
	 */
	if (old_state == CAN_STATE_STOPPED && new_state == CAN_STATE_BUS_OFF)
		return;

	skb = alloc_can_err_skb(netdev, &cf);
	if (skb) {
		enum can_state tx_state, rx_state;

		tx_state = (bec->txerr >= bec->rxerr) ?
					new_state : CAN_STATE_ERROR_ACTIVE;
		rx_state = (bec->txerr <= bec->rxerr) ?
					new_state : CAN_STATE_ERROR_ACTIVE;
		can_change_state(netdev, cf, tx_state, rx_state);
	}

	if (new_state == CAN_STATE_BUS_OFF && old_state < CAN_STATE_BUS_OFF) {
		if (!priv->can.restart_ms)
			kvaser_usb_hydra_send_simple_cmd_async
						(priv, CMD_STOP_CHIP_REQ);

		can_bus_off(netdev);
	}

	if (!skb) {
		netdev_warn(netdev, "No memory left for err_skb\n");
		return;
	}

	if (priv->can.restart_ms &&
	    old_state >= CAN_STATE_BUS_OFF &&
	    new_state < CAN_STATE_BUS_OFF)
		priv->can.can_stats.restarts++;

	cf->data[6] = bec->txerr;
	cf->data[7] = bec->rxerr;

	netif_rx(skb);
}

static void kvaser_usb_hydra_state_event(const struct kvaser_usb *dev,
					 const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv;
	struct can_berr_counter bec;
	u8 bus_status;

	priv = kvaser_usb_hydra_net_priv_from_cmd(dev, cmd);
	if (!priv)
		return;

	bus_status = cmd->chip_state_event.bus_status;
	bec.txerr = cmd->chip_state_event.tx_err_counter;
	bec.rxerr = cmd->chip_state_event.rx_err_counter;

	kvaser_usb_hydra_update_state(priv, bus_status, &bec);
	priv->bec.txerr = bec.txerr;
	priv->bec.rxerr = bec.rxerr;
}

static void kvaser_usb_hydra_error_event_parameter(const struct kvaser_usb *dev,
						   const struct kvaser_cmd *cmd)
{
	/* info1 will contain the offending cmd_no */
	switch (le16_to_cpu(cmd->error_event.info1)) {
	case CMD_START_CHIP_REQ:
		dev_warn(&dev->intf->dev,
			 "CMD_START_CHIP_REQ error in parameter\n");
		break;

	case CMD_STOP_CHIP_REQ:
		dev_warn(&dev->intf->dev,
			 "CMD_STOP_CHIP_REQ error in parameter\n");
		break;

	case CMD_FLUSH_QUEUE:
		dev_warn(&dev->intf->dev,
			 "CMD_FLUSH_QUEUE error in parameter\n");
		break;

	case CMD_SET_BUSPARAMS_REQ:
		dev_warn(&dev->intf->dev,
			 "Set bittiming failed. Error in parameter\n");
		break;

	case CMD_SET_BUSPARAMS_FD_REQ:
		dev_warn(&dev->intf->dev,
			 "Set data bittiming failed. Error in parameter\n");
		break;

	default:
		dev_warn(&dev->intf->dev,
			 "Unhandled parameter error event cmd_no (%u)\n",
			 le16_to_cpu(cmd->error_event.info1));
		break;
	}
}

static void kvaser_usb_hydra_error_event(const struct kvaser_usb *dev,
					 const struct kvaser_cmd *cmd)
{
	switch (cmd->error_event.error_code) {
	case KVASER_USB_HYDRA_ERROR_EVENT_PARAM:
		kvaser_usb_hydra_error_event_parameter(dev, cmd);
		break;

	case KVASER_USB_HYDRA_ERROR_EVENT_CAN:
		/* Wrong channel mapping?! This should never happen!
		 * info1 will contain the offending cmd_no
		 */
		dev_err(&dev->intf->dev,
			"Received CAN error event for cmd_no (%u)\n",
			le16_to_cpu(cmd->error_event.info1));
		break;

	default:
		dev_warn(&dev->intf->dev,
			 "Unhandled error event (%d)\n",
			 cmd->error_event.error_code);
		break;
	}
}

static void
kvaser_usb_hydra_error_frame(struct kvaser_usb_net_priv *priv,
			     const struct kvaser_err_frame_data *err_frame_data,
			     ktime_t hwtstamp)
{
	struct net_device *netdev = priv->netdev;
	struct net_device_stats *stats = &netdev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	struct skb_shared_hwtstamps *shhwtstamps;
	struct can_berr_counter bec;
	enum can_state new_state, old_state;
	u8 bus_status;

	priv->can.can_stats.bus_error++;
	stats->rx_errors++;

	bus_status = err_frame_data->bus_status;
	bec.txerr = err_frame_data->tx_err_counter;
	bec.rxerr = err_frame_data->rx_err_counter;

	old_state = priv->can.state;
	kvaser_usb_hydra_bus_status_to_can_state(priv, bus_status, &bec,
						 &new_state);

	skb = alloc_can_err_skb(netdev, &cf);

	if (new_state != old_state) {
		if (skb) {
			enum can_state tx_state, rx_state;

			tx_state = (bec.txerr >= bec.rxerr) ?
					new_state : CAN_STATE_ERROR_ACTIVE;
			rx_state = (bec.txerr <= bec.rxerr) ?
					new_state : CAN_STATE_ERROR_ACTIVE;

			can_change_state(netdev, cf, tx_state, rx_state);

			if (priv->can.restart_ms &&
			    old_state >= CAN_STATE_BUS_OFF &&
			    new_state < CAN_STATE_BUS_OFF)
				cf->can_id |= CAN_ERR_RESTARTED;
		}

		if (new_state == CAN_STATE_BUS_OFF) {
			if (!priv->can.restart_ms)
				kvaser_usb_hydra_send_simple_cmd_async
						(priv, CMD_STOP_CHIP_REQ);

			can_bus_off(netdev);
		}
	}

	if (!skb) {
		stats->rx_dropped++;
		netdev_warn(netdev, "No memory left for err_skb\n");
		return;
	}

	shhwtstamps = skb_hwtstamps(skb);
	shhwtstamps->hwtstamp = hwtstamp;

	cf->can_id |= CAN_ERR_BUSERROR;
	cf->data[6] = bec.txerr;
	cf->data[7] = bec.rxerr;

	netif_rx(skb);

	priv->bec.txerr = bec.txerr;
	priv->bec.rxerr = bec.rxerr;
}

static void kvaser_usb_hydra_one_shot_fail(struct kvaser_usb_net_priv *priv,
					   const struct kvaser_cmd_ext *cmd)
{
	struct net_device *netdev = priv->netdev;
	struct net_device_stats *stats = &netdev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 flags;

	skb = alloc_can_err_skb(netdev, &cf);
	if (!skb) {
		stats->rx_dropped++;
		netdev_warn(netdev, "No memory left for err_skb\n");
		return;
	}

	cf->can_id |= CAN_ERR_BUSERROR;
	flags = le32_to_cpu(cmd->tx_ack.flags);

	if (flags & KVASER_USB_HYDRA_CF_FLAG_OSM_NACK)
		cf->can_id |= CAN_ERR_ACK;
	if (flags & KVASER_USB_HYDRA_CF_FLAG_ABL) {
		cf->can_id |= CAN_ERR_LOSTARB;
		priv->can.can_stats.arbitration_lost++;
	}

	stats->tx_errors++;
	netif_rx(skb);
}

static void kvaser_usb_hydra_tx_acknowledge(const struct kvaser_usb *dev,
					    const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_tx_urb_context *context;
	struct kvaser_usb_net_priv *priv;
	unsigned long irq_flags;
	unsigned int len;
	bool one_shot_fail = false;
	bool is_err_frame = false;
	u16 transid = kvaser_usb_hydra_get_cmd_transid(cmd);

	priv = kvaser_usb_hydra_net_priv_from_cmd(dev, cmd);
	if (!priv)
		return;

	if (!netif_device_present(priv->netdev))
		return;

	if (cmd->header.cmd_no == CMD_EXTENDED) {
		struct kvaser_cmd_ext *cmd_ext = (struct kvaser_cmd_ext *)cmd;
		u32 flags = le32_to_cpu(cmd_ext->tx_ack.flags);

		if (flags & (KVASER_USB_HYDRA_CF_FLAG_OSM_NACK |
			     KVASER_USB_HYDRA_CF_FLAG_ABL)) {
			kvaser_usb_hydra_one_shot_fail(priv, cmd_ext);
			one_shot_fail = true;
		}

		is_err_frame = flags & KVASER_USB_HYDRA_CF_FLAG_TX_ACK &&
			       flags & KVASER_USB_HYDRA_CF_FLAG_ERROR_FRAME;
	}

	context = &priv->tx_contexts[transid % dev->max_tx_urbs];

	spin_lock_irqsave(&priv->tx_contexts_lock, irq_flags);

	len = can_get_echo_skb(priv->netdev, context->echo_index, NULL);
	context->echo_index = dev->max_tx_urbs;
	--priv->active_tx_contexts;
	netif_wake_queue(priv->netdev);

	spin_unlock_irqrestore(&priv->tx_contexts_lock, irq_flags);

	if (!one_shot_fail && !is_err_frame) {
		struct net_device_stats *stats = &priv->netdev->stats;

		stats->tx_packets++;
		stats->tx_bytes += len;
	}
}

static void kvaser_usb_hydra_rx_msg_std(const struct kvaser_usb *dev,
					const struct kvaser_cmd *cmd)
{
	struct kvaser_usb_net_priv *priv = NULL;
	struct can_frame *cf;
	struct sk_buff *skb;
	struct skb_shared_hwtstamps *shhwtstamps;
	struct net_device_stats *stats;
	u8 flags;
	ktime_t hwtstamp;

	priv = kvaser_usb_hydra_net_priv_from_cmd(dev, cmd);
	if (!priv)
		return;

	stats = &priv->netdev->stats;

	flags = cmd->rx_can.flags;
	hwtstamp = kvaser_usb_hydra_ktime_from_rx_cmd(dev->cfg, cmd);

	if (flags & KVASER_USB_HYDRA_CF_FLAG_ERROR_FRAME) {
		kvaser_usb_hydra_error_frame(priv, &cmd->rx_can.err_frame_data,
					     hwtstamp);
		return;
	}

	skb = alloc_can_skb(priv->netdev, &cf);
	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	shhwtstamps = skb_hwtstamps(skb);
	shhwtstamps->hwtstamp = hwtstamp;

	cf->can_id = le32_to_cpu(cmd->rx_can.id);

	if (cf->can_id &  KVASER_USB_HYDRA_EXTENDED_FRAME_ID) {
		cf->can_id &= CAN_EFF_MASK;
		cf->can_id |= CAN_EFF_FLAG;
	} else {
		cf->can_id &= CAN_SFF_MASK;
	}

	if (flags & KVASER_USB_HYDRA_CF_FLAG_OVERRUN)
		kvaser_usb_can_rx_over_error(priv->netdev);

	cf->len = can_cc_dlc2len(cmd->rx_can.dlc);

	if (flags & KVASER_USB_HYDRA_CF_FLAG_REMOTE_FRAME) {
		cf->can_id |= CAN_RTR_FLAG;
	} else {
		memcpy(cf->data, cmd->rx_can.data, cf->len);

		stats->rx_bytes += cf->len;
	}
	stats->rx_packets++;

	netif_rx(skb);
}

static void kvaser_usb_hydra_rx_msg_ext(const struct kvaser_usb *dev,
					const struct kvaser_cmd_ext *cmd)
{
	struct kvaser_cmd *std_cmd = (struct kvaser_cmd *)cmd;
	struct kvaser_usb_net_priv *priv;
	struct canfd_frame *cf;
	struct sk_buff *skb;
	struct skb_shared_hwtstamps *shhwtstamps;
	struct net_device_stats *stats;
	u32 flags;
	u8 dlc;
	u32 kcan_header;
	ktime_t hwtstamp;

	priv = kvaser_usb_hydra_net_priv_from_cmd(dev, std_cmd);
	if (!priv)
		return;

	stats = &priv->netdev->stats;

	kcan_header = le32_to_cpu(cmd->rx_can.kcan_header);
	dlc = (kcan_header & KVASER_USB_KCAN_DATA_DLC_MASK) >>
		KVASER_USB_KCAN_DATA_DLC_SHIFT;

	flags = le32_to_cpu(cmd->rx_can.flags);
	hwtstamp = kvaser_usb_hydra_ktime_from_rx_cmd(dev->cfg, std_cmd);

	if (flags & KVASER_USB_HYDRA_CF_FLAG_ERROR_FRAME) {
		kvaser_usb_hydra_error_frame(priv, &cmd->rx_can.err_frame_data,
					     hwtstamp);
		return;
	}

	if (flags & KVASER_USB_HYDRA_CF_FLAG_FDF)
		skb = alloc_canfd_skb(priv->netdev, &cf);
	else
		skb = alloc_can_skb(priv->netdev, (struct can_frame **)&cf);

	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	shhwtstamps = skb_hwtstamps(skb);
	shhwtstamps->hwtstamp = hwtstamp;

	cf->can_id = le32_to_cpu(cmd->rx_can.id);

	if (flags & KVASER_USB_HYDRA_CF_FLAG_EXTENDED_ID) {
		cf->can_id &= CAN_EFF_MASK;
		cf->can_id |= CAN_EFF_FLAG;
	} else {
		cf->can_id &= CAN_SFF_MASK;
	}

	if (flags & KVASER_USB_HYDRA_CF_FLAG_OVERRUN)
		kvaser_usb_can_rx_over_error(priv->netdev);

	if (flags & KVASER_USB_HYDRA_CF_FLAG_FDF) {
		cf->len = can_fd_dlc2len(dlc);
		if (flags & KVASER_USB_HYDRA_CF_FLAG_BRS)
			cf->flags |= CANFD_BRS;
		if (flags & KVASER_USB_HYDRA_CF_FLAG_ESI)
			cf->flags |= CANFD_ESI;
	} else {
		cf->len = can_cc_dlc2len(dlc);
	}

	if (flags & KVASER_USB_HYDRA_CF_FLAG_REMOTE_FRAME) {
		cf->can_id |= CAN_RTR_FLAG;
	} else {
		memcpy(cf->data, cmd->rx_can.kcan_payload, cf->len);

		stats->rx_bytes += cf->len;
	}
	stats->rx_packets++;

	netif_rx(skb);
}

static void kvaser_usb_hydra_handle_cmd_std(const struct kvaser_usb *dev,
					    const struct kvaser_cmd *cmd)
{
	switch (cmd->header.cmd_no) {
	case CMD_START_CHIP_RESP:
		kvaser_usb_hydra_start_chip_reply(dev, cmd);
		break;

	case CMD_STOP_CHIP_RESP:
		kvaser_usb_hydra_stop_chip_reply(dev, cmd);
		break;

	case CMD_FLUSH_QUEUE_RESP:
		kvaser_usb_hydra_flush_queue_reply(dev, cmd);
		break;

	case CMD_CHIP_STATE_EVENT:
		kvaser_usb_hydra_state_event(dev, cmd);
		break;

	case CMD_ERROR_EVENT:
		kvaser_usb_hydra_error_event(dev, cmd);
		break;

	case CMD_TX_ACKNOWLEDGE:
		kvaser_usb_hydra_tx_acknowledge(dev, cmd);
		break;

	case CMD_RX_MESSAGE:
		kvaser_usb_hydra_rx_msg_std(dev, cmd);
		break;

	/* Ignored commands */
	case CMD_SET_BUSPARAMS_RESP:
	case CMD_SET_BUSPARAMS_FD_RESP:
		break;

	default:
		dev_warn(&dev->intf->dev, "Unhandled command (%d)\n",
			 cmd->header.cmd_no);
		break;
	}
}

static void kvaser_usb_hydra_handle_cmd_ext(const struct kvaser_usb *dev,
					    const struct kvaser_cmd_ext *cmd)
{
	switch (cmd->cmd_no_ext) {
	case CMD_TX_ACKNOWLEDGE_FD:
		kvaser_usb_hydra_tx_acknowledge(dev, (struct kvaser_cmd *)cmd);
		break;

	case CMD_RX_MESSAGE_FD:
		kvaser_usb_hydra_rx_msg_ext(dev, cmd);
		break;

	default:
		dev_warn(&dev->intf->dev, "Unhandled extended command (%d)\n",
			 cmd->header.cmd_no);
		break;
	}
}

static void kvaser_usb_hydra_handle_cmd(const struct kvaser_usb *dev,
					const struct kvaser_cmd *cmd)
{
		if (cmd->header.cmd_no == CMD_EXTENDED)
			kvaser_usb_hydra_handle_cmd_ext
					(dev, (struct kvaser_cmd_ext *)cmd);
		else
			kvaser_usb_hydra_handle_cmd_std(dev, cmd);
}

static void *
kvaser_usb_hydra_frame_to_cmd_ext(const struct kvaser_usb_net_priv *priv,
				  const struct sk_buff *skb, int *cmd_len,
				  u16 transid)
{
	struct kvaser_usb *dev = priv->dev;
	struct kvaser_cmd_ext *cmd;
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	u8 dlc = can_fd_len2dlc(cf->len);
	u8 nbr_of_bytes = cf->len;
	u32 flags;
	u32 id;
	u32 kcan_id;
	u32 kcan_header;

	cmd = kcalloc(1, sizeof(struct kvaser_cmd_ext), GFP_ATOMIC);
	if (!cmd)
		return NULL;

	kvaser_usb_hydra_set_cmd_dest_he
			((struct kvaser_cmd *)cmd,
			 dev->card_data.hydra.channel_to_he[priv->channel]);
	kvaser_usb_hydra_set_cmd_transid((struct kvaser_cmd *)cmd, transid);

	cmd->header.cmd_no = CMD_EXTENDED;
	cmd->cmd_no_ext = CMD_TX_CAN_MESSAGE_FD;

	*cmd_len = ALIGN(sizeof(struct kvaser_cmd_ext) -
			 sizeof(cmd->tx_can.kcan_payload) + nbr_of_bytes,
			 8);

	cmd->len = cpu_to_le16(*cmd_len);

	cmd->tx_can.databytes = nbr_of_bytes;
	cmd->tx_can.dlc = dlc;

	if (cf->can_id & CAN_EFF_FLAG) {
		id = cf->can_id & CAN_EFF_MASK;
		flags = KVASER_USB_HYDRA_CF_FLAG_EXTENDED_ID;
		kcan_id = (cf->can_id & CAN_EFF_MASK) |
			  KVASER_USB_KCAN_DATA_IDE | KVASER_USB_KCAN_DATA_SRR;
	} else {
		id = cf->can_id & CAN_SFF_MASK;
		flags = 0;
		kcan_id = cf->can_id & CAN_SFF_MASK;
	}

	if (cf->can_id & CAN_ERR_FLAG)
		flags |= KVASER_USB_HYDRA_CF_FLAG_ERROR_FRAME;

	kcan_header = ((dlc << KVASER_USB_KCAN_DATA_DLC_SHIFT) &
				KVASER_USB_KCAN_DATA_DLC_MASK) |
			KVASER_USB_KCAN_DATA_AREQ |
			(priv->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT ?
				KVASER_USB_KCAN_DATA_OSM : 0);

	if (can_is_canfd_skb(skb)) {
		kcan_header |= KVASER_USB_KCAN_DATA_FDF |
			       (cf->flags & CANFD_BRS ?
					KVASER_USB_KCAN_DATA_BRS : 0);
	} else {
		if (cf->can_id & CAN_RTR_FLAG) {
			kcan_id |= KVASER_USB_KCAN_DATA_RTR;
			cmd->tx_can.databytes = 0;
			flags |= KVASER_USB_HYDRA_CF_FLAG_REMOTE_FRAME;
		}
	}

	cmd->tx_can.kcan_id = cpu_to_le32(kcan_id);
	cmd->tx_can.id = cpu_to_le32(id);
	cmd->tx_can.flags = cpu_to_le32(flags);
	cmd->tx_can.kcan_header = cpu_to_le32(kcan_header);

	memcpy(cmd->tx_can.kcan_payload, cf->data, nbr_of_bytes);

	return cmd;
}

static void *
kvaser_usb_hydra_frame_to_cmd_std(const struct kvaser_usb_net_priv *priv,
				  const struct sk_buff *skb, int *cmd_len,
				  u16 transid)
{
	struct kvaser_usb *dev = priv->dev;
	struct kvaser_cmd *cmd;
	struct can_frame *cf = (struct can_frame *)skb->data;
	u32 flags;
	u32 id;

	cmd = kcalloc(1, sizeof(struct kvaser_cmd), GFP_ATOMIC);
	if (!cmd)
		return NULL;

	kvaser_usb_hydra_set_cmd_dest_he
		(cmd, dev->card_data.hydra.channel_to_he[priv->channel]);
	kvaser_usb_hydra_set_cmd_transid(cmd, transid);

	cmd->header.cmd_no = CMD_TX_CAN_MESSAGE;

	*cmd_len = ALIGN(sizeof(struct kvaser_cmd), 8);

	if (cf->can_id & CAN_EFF_FLAG) {
		id = (cf->can_id & CAN_EFF_MASK);
		id |= KVASER_USB_HYDRA_EXTENDED_FRAME_ID;
	} else {
		id = cf->can_id & CAN_SFF_MASK;
	}

	cmd->tx_can.dlc = cf->len;

	flags = (cf->can_id & CAN_EFF_FLAG ?
		 KVASER_USB_HYDRA_CF_FLAG_EXTENDED_ID : 0);

	if (cf->can_id & CAN_RTR_FLAG)
		flags |= KVASER_USB_HYDRA_CF_FLAG_REMOTE_FRAME;

	flags |= (cf->can_id & CAN_ERR_FLAG ?
		  KVASER_USB_HYDRA_CF_FLAG_ERROR_FRAME : 0);

	cmd->tx_can.id = cpu_to_le32(id);
	cmd->tx_can.flags = flags;

	memcpy(cmd->tx_can.data, cf->data, cf->len);

	return cmd;
}

static int kvaser_usb_hydra_set_mode(struct net_device *netdev,
				     enum can_mode mode)
{
	int err = 0;

	switch (mode) {
	case CAN_MODE_START:
		/* CAN controller automatically recovers from BUS_OFF */
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static int kvaser_usb_hydra_set_bittiming(struct net_device *netdev)
{
	struct kvaser_cmd *cmd;
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct kvaser_usb *dev = priv->dev;
	int tseg1 = bt->prop_seg + bt->phase_seg1;
	int tseg2 = bt->phase_seg2;
	int sjw = bt->sjw;
	int err;

	cmd = kcalloc(1, sizeof(struct kvaser_cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->header.cmd_no = CMD_SET_BUSPARAMS_REQ;
	cmd->set_busparams_req.bitrate = cpu_to_le32(bt->bitrate);
	cmd->set_busparams_req.sjw = (u8)sjw;
	cmd->set_busparams_req.tseg1 = (u8)tseg1;
	cmd->set_busparams_req.tseg2 = (u8)tseg2;
	cmd->set_busparams_req.nsamples = 1;

	kvaser_usb_hydra_set_cmd_dest_he
		(cmd, dev->card_data.hydra.channel_to_he[priv->channel]);
	kvaser_usb_hydra_set_cmd_transid
				(cmd, kvaser_usb_hydra_get_next_transid(dev));

	err = kvaser_usb_send_cmd(dev, cmd, kvaser_usb_hydra_cmd_size(cmd));

	kfree(cmd);

	return err;
}

static int kvaser_usb_hydra_set_data_bittiming(struct net_device *netdev)
{
	struct kvaser_cmd *cmd;
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	struct can_bittiming *dbt = &priv->can.data_bittiming;
	struct kvaser_usb *dev = priv->dev;
	int tseg1 = dbt->prop_seg + dbt->phase_seg1;
	int tseg2 = dbt->phase_seg2;
	int sjw = dbt->sjw;
	int err;

	cmd = kcalloc(1, sizeof(struct kvaser_cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->header.cmd_no = CMD_SET_BUSPARAMS_FD_REQ;
	cmd->set_busparams_req.bitrate_d = cpu_to_le32(dbt->bitrate);
	cmd->set_busparams_req.sjw_d = (u8)sjw;
	cmd->set_busparams_req.tseg1_d = (u8)tseg1;
	cmd->set_busparams_req.tseg2_d = (u8)tseg2;
	cmd->set_busparams_req.nsamples_d = 1;

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD) {
		if (priv->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO)
			cmd->set_busparams_req.canfd_mode =
					KVASER_USB_HYDRA_BUS_MODE_NONISO;
		else
			cmd->set_busparams_req.canfd_mode =
					KVASER_USB_HYDRA_BUS_MODE_CANFD_ISO;
	}

	kvaser_usb_hydra_set_cmd_dest_he
		(cmd, dev->card_data.hydra.channel_to_he[priv->channel]);
	kvaser_usb_hydra_set_cmd_transid
				(cmd, kvaser_usb_hydra_get_next_transid(dev));

	err = kvaser_usb_send_cmd(dev, cmd, kvaser_usb_hydra_cmd_size(cmd));

	kfree(cmd);

	return err;
}

static int kvaser_usb_hydra_get_berr_counter(const struct net_device *netdev,
					     struct can_berr_counter *bec)
{
	struct kvaser_usb_net_priv *priv = netdev_priv(netdev);
	int err;

	err = kvaser_usb_hydra_send_simple_cmd(priv->dev,
					       CMD_GET_CHIP_STATE_REQ,
					       priv->channel);
	if (err)
		return err;

	*bec = priv->bec;

	return 0;
}

static int kvaser_usb_hydra_setup_endpoints(struct kvaser_usb *dev)
{
	const struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *ep;
	int i;

	iface_desc = dev->intf->cur_altsetting;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		ep = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in && usb_endpoint_is_bulk_in(ep) &&
		    ep->bEndpointAddress == KVASER_USB_HYDRA_BULK_EP_IN_ADDR)
			dev->bulk_in = ep;

		if (!dev->bulk_out && usb_endpoint_is_bulk_out(ep) &&
		    ep->bEndpointAddress == KVASER_USB_HYDRA_BULK_EP_OUT_ADDR)
			dev->bulk_out = ep;

		if (dev->bulk_in && dev->bulk_out)
			return 0;
	}

	return -ENODEV;
}

static int kvaser_usb_hydra_init_card(struct kvaser_usb *dev)
{
	int err;
	unsigned int i;
	struct kvaser_usb_dev_card_data_hydra *card_data =
							&dev->card_data.hydra;

	card_data->transid = KVASER_USB_HYDRA_MIN_TRANSID;
	spin_lock_init(&card_data->transid_lock);

	memset(card_data->usb_rx_leftover, 0, KVASER_USB_HYDRA_MAX_CMD_LEN);
	card_data->usb_rx_leftover_len = 0;
	spin_lock_init(&card_data->usb_rx_leftover_lock);

	memset(card_data->channel_to_he, KVASER_USB_HYDRA_HE_ADDRESS_ILLEGAL,
	       sizeof(card_data->channel_to_he));
	card_data->sysdbg_he = 0;

	for (i = 0; i < KVASER_USB_MAX_NET_DEVICES; i++) {
		err = kvaser_usb_hydra_map_channel
					(dev,
					 (KVASER_USB_HYDRA_TRANSID_CANHE | i),
					 i, "CAN");
		if (err) {
			dev_err(&dev->intf->dev,
				"CMD_MAP_CHANNEL_REQ failed for CAN%u\n", i);
			return err;
		}
	}

	err = kvaser_usb_hydra_map_channel(dev, KVASER_USB_HYDRA_TRANSID_SYSDBG,
					   0, "SYSDBG");
	if (err) {
		dev_err(&dev->intf->dev,
			"CMD_MAP_CHANNEL_REQ failed for SYSDBG\n");
		return err;
	}

	return 0;
}

static int kvaser_usb_hydra_get_software_info(struct kvaser_usb *dev)
{
	struct kvaser_cmd cmd;
	int err;

	err = kvaser_usb_hydra_send_simple_cmd(dev, CMD_GET_SOFTWARE_INFO_REQ,
					       -1);
	if (err)
		return err;

	memset(&cmd, 0, sizeof(struct kvaser_cmd));
	err = kvaser_usb_hydra_wait_cmd(dev, CMD_GET_SOFTWARE_INFO_RESP, &cmd);
	if (err)
		return err;

	dev->max_tx_urbs = min_t(unsigned int, KVASER_USB_MAX_TX_URBS,
				 le16_to_cpu(cmd.sw_info.max_outstanding_tx));

	return 0;
}

static int kvaser_usb_hydra_get_software_details(struct kvaser_usb *dev)
{
	struct kvaser_cmd *cmd;
	int err;
	u32 flags;
	struct kvaser_usb_dev_card_data *card_data = &dev->card_data;

	cmd = kcalloc(1, sizeof(struct kvaser_cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->header.cmd_no = CMD_GET_SOFTWARE_DETAILS_REQ;
	cmd->sw_detail_req.use_ext_cmd = 1;
	kvaser_usb_hydra_set_cmd_dest_he
				(cmd, KVASER_USB_HYDRA_HE_ADDRESS_ILLEGAL);

	kvaser_usb_hydra_set_cmd_transid
				(cmd, kvaser_usb_hydra_get_next_transid(dev));

	err = kvaser_usb_send_cmd(dev, cmd, kvaser_usb_hydra_cmd_size(cmd));
	if (err)
		goto end;

	err = kvaser_usb_hydra_wait_cmd(dev, CMD_GET_SOFTWARE_DETAILS_RESP,
					cmd);
	if (err)
		goto end;

	dev->fw_version = le32_to_cpu(cmd->sw_detail_res.sw_version);
	flags = le32_to_cpu(cmd->sw_detail_res.sw_flags);

	if (flags & KVASER_USB_HYDRA_SW_FLAG_FW_BAD) {
		dev_err(&dev->intf->dev,
			"Bad firmware, device refuse to run!\n");
		err = -EINVAL;
		goto end;
	}

	if (flags & KVASER_USB_HYDRA_SW_FLAG_FW_BETA)
		dev_info(&dev->intf->dev, "Beta firmware in use\n");

	if (flags & KVASER_USB_HYDRA_SW_FLAG_EXT_CAP)
		card_data->capabilities |= KVASER_USB_CAP_EXT_CAP;

	if (flags & KVASER_USB_HYDRA_SW_FLAG_EXT_CMD)
		card_data->capabilities |= KVASER_USB_HYDRA_CAP_EXT_CMD;

	if (flags & KVASER_USB_HYDRA_SW_FLAG_CANFD)
		card_data->ctrlmode_supported |= CAN_CTRLMODE_FD;

	if (flags & KVASER_USB_HYDRA_SW_FLAG_NONISO)
		card_data->ctrlmode_supported |= CAN_CTRLMODE_FD_NON_ISO;

	if (flags &  KVASER_USB_HYDRA_SW_FLAG_FREQ_80M)
		dev->cfg = &kvaser_usb_hydra_dev_cfg_kcan;
	else if (flags & KVASER_USB_HYDRA_SW_FLAG_CAN_FREQ_80M)
		dev->cfg = &kvaser_usb_hydra_dev_cfg_rt;
	else
		dev->cfg = &kvaser_usb_hydra_dev_cfg_flexc;

end:
	kfree(cmd);

	return err;
}

static int kvaser_usb_hydra_get_card_info(struct kvaser_usb *dev)
{
	struct kvaser_cmd cmd;
	int err;

	err = kvaser_usb_hydra_send_simple_cmd(dev, CMD_GET_CARD_INFO_REQ, -1);
	if (err)
		return err;

	memset(&cmd, 0, sizeof(struct kvaser_cmd));
	err = kvaser_usb_hydra_wait_cmd(dev, CMD_GET_CARD_INFO_RESP, &cmd);
	if (err)
		return err;

	dev->nchannels = cmd.card_info.nchannels;
	if (dev->nchannels > KVASER_USB_MAX_NET_DEVICES)
		return -EINVAL;

	return 0;
}

static int kvaser_usb_hydra_get_capabilities(struct kvaser_usb *dev)
{
	int err;
	u16 status;

	if (!(dev->card_data.capabilities & KVASER_USB_CAP_EXT_CAP)) {
		dev_info(&dev->intf->dev,
			 "No extended capability support. Upgrade your device.\n");
		return 0;
	}

	err = kvaser_usb_hydra_get_single_capability
					(dev,
					 KVASER_USB_HYDRA_CAP_CMD_LISTEN_MODE,
					 &status);
	if (err)
		return err;
	if (status)
		dev_info(&dev->intf->dev,
			 "KVASER_USB_HYDRA_CAP_CMD_LISTEN_MODE failed %u\n",
			 status);

	err = kvaser_usb_hydra_get_single_capability
					(dev,
					 KVASER_USB_HYDRA_CAP_CMD_ERR_REPORT,
					 &status);
	if (err)
		return err;
	if (status)
		dev_info(&dev->intf->dev,
			 "KVASER_USB_HYDRA_CAP_CMD_ERR_REPORT failed %u\n",
			 status);

	err = kvaser_usb_hydra_get_single_capability
					(dev, KVASER_USB_HYDRA_CAP_CMD_ONE_SHOT,
					 &status);
	if (err)
		return err;
	if (status)
		dev_info(&dev->intf->dev,
			 "KVASER_USB_HYDRA_CAP_CMD_ONE_SHOT failed %u\n",
			 status);

	return 0;
}

static int kvaser_usb_hydra_set_opt_mode(const struct kvaser_usb_net_priv *priv)
{
	struct kvaser_usb *dev = priv->dev;
	struct kvaser_cmd *cmd;
	int err;

	if ((priv->can.ctrlmode &
	    (CAN_CTRLMODE_FD | CAN_CTRLMODE_FD_NON_ISO)) ==
	    CAN_CTRLMODE_FD_NON_ISO) {
		netdev_warn(priv->netdev,
			    "CTRLMODE_FD shall be on if CTRLMODE_FD_NON_ISO is on\n");
		return -EINVAL;
	}

	cmd = kcalloc(1, sizeof(struct kvaser_cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->header.cmd_no = CMD_SET_DRIVERMODE_REQ;
	kvaser_usb_hydra_set_cmd_dest_he
		(cmd, dev->card_data.hydra.channel_to_he[priv->channel]);
	kvaser_usb_hydra_set_cmd_transid
				(cmd, kvaser_usb_hydra_get_next_transid(dev));
	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		cmd->set_ctrlmode.mode = KVASER_USB_HYDRA_CTRLMODE_LISTEN;
	else
		cmd->set_ctrlmode.mode = KVASER_USB_HYDRA_CTRLMODE_NORMAL;

	err = kvaser_usb_send_cmd(dev, cmd, kvaser_usb_hydra_cmd_size(cmd));
	kfree(cmd);

	return err;
}

static int kvaser_usb_hydra_start_chip(struct kvaser_usb_net_priv *priv)
{
	int err;

	init_completion(&priv->start_comp);

	err = kvaser_usb_hydra_send_simple_cmd(priv->dev, CMD_START_CHIP_REQ,
					       priv->channel);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&priv->start_comp,
					 msecs_to_jiffies(KVASER_USB_TIMEOUT)))
		return -ETIMEDOUT;

	return 0;
}

static int kvaser_usb_hydra_stop_chip(struct kvaser_usb_net_priv *priv)
{
	int err;

	init_completion(&priv->stop_comp);

	/* Make sure we do not report invalid BUS_OFF from CMD_CHIP_STATE_EVENT
	 * see comment in kvaser_usb_hydra_update_state()
	 */
	priv->can.state = CAN_STATE_STOPPED;

	err = kvaser_usb_hydra_send_simple_cmd(priv->dev, CMD_STOP_CHIP_REQ,
					       priv->channel);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&priv->stop_comp,
					 msecs_to_jiffies(KVASER_USB_TIMEOUT)))
		return -ETIMEDOUT;

	return 0;
}

static int kvaser_usb_hydra_flush_queue(struct kvaser_usb_net_priv *priv)
{
	int err;

	init_completion(&priv->flush_comp);

	err = kvaser_usb_hydra_send_simple_cmd(priv->dev, CMD_FLUSH_QUEUE,
					       priv->channel);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&priv->flush_comp,
					 msecs_to_jiffies(KVASER_USB_TIMEOUT)))
		return -ETIMEDOUT;

	return 0;
}

/* A single extended hydra command can be transmitted in multiple transfers
 * We have to buffer partial hydra commands, and handle them on next callback.
 */
static void kvaser_usb_hydra_read_bulk_callback(struct kvaser_usb *dev,
						void *buf, int len)
{
	unsigned long irq_flags;
	struct kvaser_cmd *cmd;
	int pos = 0;
	size_t cmd_len;
	struct kvaser_usb_dev_card_data_hydra *card_data =
							&dev->card_data.hydra;
	int usb_rx_leftover_len;
	spinlock_t *usb_rx_leftover_lock = &card_data->usb_rx_leftover_lock;

	spin_lock_irqsave(usb_rx_leftover_lock, irq_flags);
	usb_rx_leftover_len = card_data->usb_rx_leftover_len;
	if (usb_rx_leftover_len) {
		int remaining_bytes;

		cmd = (struct kvaser_cmd *)card_data->usb_rx_leftover;

		cmd_len = kvaser_usb_hydra_cmd_size(cmd);

		remaining_bytes = min_t(unsigned int, len,
					cmd_len - usb_rx_leftover_len);
		/* Make sure we do not overflow usb_rx_leftover */
		if (remaining_bytes + usb_rx_leftover_len >
						KVASER_USB_HYDRA_MAX_CMD_LEN) {
			dev_err(&dev->intf->dev, "Format error\n");
			spin_unlock_irqrestore(usb_rx_leftover_lock, irq_flags);
			return;
		}

		memcpy(card_data->usb_rx_leftover + usb_rx_leftover_len, buf,
		       remaining_bytes);
		pos += remaining_bytes;

		if (remaining_bytes + usb_rx_leftover_len == cmd_len) {
			kvaser_usb_hydra_handle_cmd(dev, cmd);
			usb_rx_leftover_len = 0;
		} else {
			/* Command still not complete */
			usb_rx_leftover_len += remaining_bytes;
		}
		card_data->usb_rx_leftover_len = usb_rx_leftover_len;
	}
	spin_unlock_irqrestore(usb_rx_leftover_lock, irq_flags);

	while (pos < len) {
		cmd = buf + pos;

		cmd_len = kvaser_usb_hydra_cmd_size(cmd);

		if (pos + cmd_len > len) {
			/* We got first part of a command */
			int leftover_bytes;

			leftover_bytes = len - pos;
			/* Make sure we do not overflow usb_rx_leftover */
			if (leftover_bytes > KVASER_USB_HYDRA_MAX_CMD_LEN) {
				dev_err(&dev->intf->dev, "Format error\n");
				return;
			}
			spin_lock_irqsave(usb_rx_leftover_lock, irq_flags);
			memcpy(card_data->usb_rx_leftover, buf + pos,
			       leftover_bytes);
			card_data->usb_rx_leftover_len = leftover_bytes;
			spin_unlock_irqrestore(usb_rx_leftover_lock, irq_flags);
			break;
		}

		kvaser_usb_hydra_handle_cmd(dev, cmd);
		pos += cmd_len;
	}
}

static void *
kvaser_usb_hydra_frame_to_cmd(const struct kvaser_usb_net_priv *priv,
			      const struct sk_buff *skb, int *cmd_len,
			      u16 transid)
{
	void *buf;

	if (priv->dev->card_data.capabilities & KVASER_USB_HYDRA_CAP_EXT_CMD)
		buf = kvaser_usb_hydra_frame_to_cmd_ext(priv, skb, cmd_len,
							transid);
	else
		buf = kvaser_usb_hydra_frame_to_cmd_std(priv, skb, cmd_len,
							transid);

	return buf;
}

const struct kvaser_usb_dev_ops kvaser_usb_hydra_dev_ops = {
	.dev_set_mode = kvaser_usb_hydra_set_mode,
	.dev_set_bittiming = kvaser_usb_hydra_set_bittiming,
	.dev_set_data_bittiming = kvaser_usb_hydra_set_data_bittiming,
	.dev_get_berr_counter = kvaser_usb_hydra_get_berr_counter,
	.dev_setup_endpoints = kvaser_usb_hydra_setup_endpoints,
	.dev_init_card = kvaser_usb_hydra_init_card,
	.dev_get_software_info = kvaser_usb_hydra_get_software_info,
	.dev_get_software_details = kvaser_usb_hydra_get_software_details,
	.dev_get_card_info = kvaser_usb_hydra_get_card_info,
	.dev_get_capabilities = kvaser_usb_hydra_get_capabilities,
	.dev_set_opt_mode = kvaser_usb_hydra_set_opt_mode,
	.dev_start_chip = kvaser_usb_hydra_start_chip,
	.dev_stop_chip = kvaser_usb_hydra_stop_chip,
	.dev_reset_chip = NULL,
	.dev_flush_queue = kvaser_usb_hydra_flush_queue,
	.dev_read_bulk_callback = kvaser_usb_hydra_read_bulk_callback,
	.dev_frame_to_cmd = kvaser_usb_hydra_frame_to_cmd,
};

static const struct kvaser_usb_dev_cfg kvaser_usb_hydra_dev_cfg_kcan = {
	.clock = {
		.freq = 80 * MEGA /* Hz */,
	},
	.timestamp_freq = 80,
	.bittiming_const = &kvaser_usb_hydra_kcan_bittiming_c,
	.data_bittiming_const = &kvaser_usb_hydra_kcan_bittiming_c,
};

static const struct kvaser_usb_dev_cfg kvaser_usb_hydra_dev_cfg_flexc = {
	.clock = {
		.freq = 24 * MEGA /* Hz */,
	},
	.timestamp_freq = 1,
	.bittiming_const = &kvaser_usb_hydra_flexc_bittiming_c,
};

static const struct kvaser_usb_dev_cfg kvaser_usb_hydra_dev_cfg_rt = {
	.clock = {
		.freq = 80 * MEGA /* Hz */,
	},
	.timestamp_freq = 24,
	.bittiming_const = &kvaser_usb_hydra_rt_bittiming_c,
	.data_bittiming_const = &kvaser_usb_hydra_rtd_bittiming_c,
};
