// SPDX-License-Identifier: GPL-2.0-only
/*
 * CAN driver for PEAK System PCAN-USB FD / PCAN-USB Pro FD adapter
 *
 * Copyright (C) 2013-2014 Stephane Grosjean <s.grosjean@peak-system.com>
 */
#include <linux/netdevice.h>
#include <linux/usb.h>
#include <linux/module.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/dev/peak_canfd.h>

#include "pcan_usb_core.h"
#include "pcan_usb_pro.h"

#define PCAN_USBPROFD_CHANNEL_COUNT	2
#define PCAN_USBFD_CHANNEL_COUNT	1

/* PCAN-USB Pro FD adapter internal clock (Hz) */
#define PCAN_UFD_CRYSTAL_HZ		80000000

#define PCAN_UFD_CMD_BUFFER_SIZE	512
#define PCAN_UFD_LOSPD_PKT_SIZE		64

/* PCAN-USB Pro FD command timeout (ms.) */
#define PCAN_UFD_CMD_TIMEOUT_MS		1000

/* PCAN-USB Pro FD rx/tx buffers size */
#define PCAN_UFD_RX_BUFFER_SIZE		2048
#define PCAN_UFD_TX_BUFFER_SIZE		512

/* read some versions info from the hw device */
struct __packed pcan_ufd_fw_info {
	__le16	size_of;	/* sizeof this */
	__le16	type;		/* type of this structure */
	u8	hw_type;	/* Type of hardware (HW_TYPE_xxx) */
	u8	bl_version[3];	/* Bootloader version */
	u8	hw_version;	/* Hardware version (PCB) */
	u8	fw_version[3];	/* Firmware version */
	__le32	dev_id[2];	/* "device id" per CAN */
	__le32	ser_no;		/* S/N */
	__le32	flags;		/* special functions */
};

/* handle device specific info used by the netdevices */
struct pcan_usb_fd_if {
	struct peak_usb_device	*dev[PCAN_USB_MAX_CHANNEL];
	struct pcan_ufd_fw_info	fw_info;
	struct peak_time_ref	time_ref;
	int			cm_ignore_count;
	int			dev_opened_count;
};

/* device information */
struct pcan_usb_fd_device {
	struct peak_usb_device	dev;
	struct can_berr_counter	bec;
	struct pcan_usb_fd_if	*usb_if;
	u8			*cmd_buffer_addr;
};

/* Extended USB commands (non uCAN commands) */

/* Clock Modes command */
#define PCAN_UFD_CMD_CLK_SET		0x80

#define PCAN_UFD_CLK_80MHZ		0x0
#define PCAN_UFD_CLK_60MHZ		0x1
#define PCAN_UFD_CLK_40MHZ		0x2
#define PCAN_UFD_CLK_30MHZ		0x3
#define PCAN_UFD_CLK_24MHZ		0x4
#define PCAN_UFD_CLK_20MHZ		0x5
#define PCAN_UFD_CLK_DEF		PCAN_UFD_CLK_80MHZ

struct __packed pcan_ufd_clock {
	__le16	opcode_channel;

	u8	mode;
	u8	unused[5];
};

/* LED control command */
#define PCAN_UFD_CMD_LED_SET		0x86

#define PCAN_UFD_LED_DEV		0x00
#define PCAN_UFD_LED_FAST		0x01
#define PCAN_UFD_LED_SLOW		0x02
#define PCAN_UFD_LED_ON			0x03
#define PCAN_UFD_LED_OFF		0x04
#define PCAN_UFD_LED_DEF		PCAN_UFD_LED_DEV

struct __packed pcan_ufd_led {
	__le16	opcode_channel;

	u8	mode;
	u8	unused[5];
};

/* Extended usage of uCAN commands CMD_xxx_xx_OPTION for PCAN-USB Pro FD */
#define PCAN_UFD_FLTEXT_CALIBRATION	0x8000

struct __packed pcan_ufd_options {
	__le16	opcode_channel;

	__le16	ucan_mask;
	u16	unused;
	__le16	usb_mask;
};

/* Extended usage of uCAN messages for PCAN-USB Pro FD */
#define PCAN_UFD_MSG_CALIBRATION	0x100

struct __packed pcan_ufd_ts_msg {
	__le16	size;
	__le16	type;
	__le32	ts_low;
	__le32	ts_high;
	__le16	usb_frame_index;
	u16	unused;
};

#define PCAN_UFD_MSG_OVERRUN		0x101

#define PCAN_UFD_OVMSG_CHANNEL(o)	((o)->channel & 0xf)

struct __packed pcan_ufd_ovr_msg {
	__le16	size;
	__le16	type;
	__le32	ts_low;
	__le32	ts_high;
	u8	channel;
	u8	unused[3];
};

static inline int pufd_omsg_get_channel(struct pcan_ufd_ovr_msg *om)
{
	return om->channel & 0xf;
}

/* Clock mode frequency values */
static const u32 pcan_usb_fd_clk_freq[6] = {
	[PCAN_UFD_CLK_80MHZ] = 80000000,
	[PCAN_UFD_CLK_60MHZ] = 60000000,
	[PCAN_UFD_CLK_40MHZ] = 40000000,
	[PCAN_UFD_CLK_30MHZ] = 30000000,
	[PCAN_UFD_CLK_24MHZ] = 24000000,
	[PCAN_UFD_CLK_20MHZ] = 20000000
};

/* return a device USB interface */
static inline
struct pcan_usb_fd_if *pcan_usb_fd_dev_if(struct peak_usb_device *dev)
{
	struct pcan_usb_fd_device *pdev =
			container_of(dev, struct pcan_usb_fd_device, dev);
	return pdev->usb_if;
}

/* return a device USB commands buffer */
static inline void *pcan_usb_fd_cmd_buffer(struct peak_usb_device *dev)
{
	struct pcan_usb_fd_device *pdev =
			container_of(dev, struct pcan_usb_fd_device, dev);
	return pdev->cmd_buffer_addr;
}

/* send PCAN-USB Pro FD commands synchronously */
static int pcan_usb_fd_send_cmd(struct peak_usb_device *dev, void *cmd_tail)
{
	void *cmd_head = pcan_usb_fd_cmd_buffer(dev);
	int err = 0;
	u8 *packet_ptr;
	int packet_len;
	ptrdiff_t cmd_len;

	/* usb device unregistered? */
	if (!(dev->state & PCAN_USB_STATE_CONNECTED))
		return 0;

	/* if a packet is not filled completely by commands, the command list
	 * is terminated with an "end of collection" record.
	 */
	cmd_len = cmd_tail - cmd_head;
	if (cmd_len <= (PCAN_UFD_CMD_BUFFER_SIZE - sizeof(u64))) {
		memset(cmd_tail, 0xff, sizeof(u64));
		cmd_len += sizeof(u64);
	}

	packet_ptr = cmd_head;
	packet_len = cmd_len;

	/* firmware is not able to re-assemble 512 bytes buffer in full-speed */
	if (unlikely(dev->udev->speed != USB_SPEED_HIGH))
		packet_len = min(packet_len, PCAN_UFD_LOSPD_PKT_SIZE);

	do {
		err = usb_bulk_msg(dev->udev,
				   usb_sndbulkpipe(dev->udev,
						   PCAN_USBPRO_EP_CMDOUT),
				   packet_ptr, packet_len,
				   NULL, PCAN_UFD_CMD_TIMEOUT_MS);
		if (err) {
			netdev_err(dev->netdev,
				   "sending command failure: %d\n", err);
			break;
		}

		packet_ptr += packet_len;
		cmd_len -= packet_len;

		if (cmd_len < PCAN_UFD_LOSPD_PKT_SIZE)
			packet_len = cmd_len;

	} while (packet_len > 0);

	return err;
}

/* build the commands list in the given buffer, to enter operational mode */
static int pcan_usb_fd_build_restart_cmd(struct peak_usb_device *dev, u8 *buf)
{
	struct pucan_wr_err_cnt *prc;
	struct pucan_command *cmd;
	u8 *pc = buf;

	/* 1st, reset error counters: */
	prc = (struct pucan_wr_err_cnt *)pc;
	prc->opcode_channel = pucan_cmd_opcode_channel(dev->ctrl_idx,
						       PUCAN_CMD_WR_ERR_CNT);

	/* select both counters */
	prc->sel_mask = cpu_to_le16(PUCAN_WRERRCNT_TE|PUCAN_WRERRCNT_RE);

	/* and reset their values */
	prc->tx_counter = 0;
	prc->rx_counter = 0;

	/* moves the pointer forward */
	pc += sizeof(struct pucan_wr_err_cnt);

	/* add command to switch from ISO to non-ISO mode, if fw allows it */
	if (dev->can.ctrlmode_supported & CAN_CTRLMODE_FD_NON_ISO) {
		struct pucan_options *puo = (struct pucan_options *)pc;

		puo->opcode_channel =
			(dev->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO) ?
			pucan_cmd_opcode_channel(dev->ctrl_idx,
						 PUCAN_CMD_CLR_DIS_OPTION) :
			pucan_cmd_opcode_channel(dev->ctrl_idx,
						 PUCAN_CMD_SET_EN_OPTION);

		puo->options = cpu_to_le16(PUCAN_OPTION_CANDFDISO);

		/* to be sure that no other extended bits will be taken into
		 * account
		 */
		puo->unused = 0;

		/* moves the pointer forward */
		pc += sizeof(struct pucan_options);
	}

	/* next, go back to operational mode */
	cmd = (struct pucan_command *)pc;
	cmd->opcode_channel = pucan_cmd_opcode_channel(dev->ctrl_idx,
				(dev->can.ctrlmode & CAN_CTRLMODE_LISTENONLY) ?
						PUCAN_CMD_LISTEN_ONLY_MODE :
						PUCAN_CMD_NORMAL_MODE);
	pc += sizeof(struct pucan_command);

	return pc - buf;
}

/* set CAN bus on/off */
static int pcan_usb_fd_set_bus(struct peak_usb_device *dev, u8 onoff)
{
	u8 *pc = pcan_usb_fd_cmd_buffer(dev);
	int l;

	if (onoff) {
		/* build the cmds list to enter operational mode */
		l = pcan_usb_fd_build_restart_cmd(dev, pc);
	} else {
		struct pucan_command *cmd = (struct pucan_command *)pc;

		/* build cmd to go back to reset mode */
		cmd->opcode_channel = pucan_cmd_opcode_channel(dev->ctrl_idx,
							PUCAN_CMD_RESET_MODE);
		l = sizeof(struct pucan_command);
	}

	/* send the command */
	return pcan_usb_fd_send_cmd(dev, pc + l);
}

/* set filtering masks:
 *
 *	idx  in range [0..63] selects a row #idx, all rows otherwise
 *	mask in range [0..0xffffffff] defines up to 32 CANIDs in the row(s)
 *
 *	Each bit of this 64 x 32 bits array defines a CANID value:
 *
 *	bit[i,j] = 1 implies that CANID=(i x 32)+j will be received, while
 *	bit[i,j] = 0 implies that CANID=(i x 32)+j will be discarded.
 */
static int pcan_usb_fd_set_filter_std(struct peak_usb_device *dev, int idx,
				      u32 mask)
{
	struct pucan_filter_std *cmd = pcan_usb_fd_cmd_buffer(dev);
	int i, n;

	/* select all rows when idx is out of range [0..63] */
	if ((idx < 0) || (idx >= (1 << PUCAN_FLTSTD_ROW_IDX_BITS))) {
		n = 1 << PUCAN_FLTSTD_ROW_IDX_BITS;
		idx = 0;

	/* select the row (and only the row) otherwise */
	} else {
		n = idx + 1;
	}

	for (i = idx; i < n; i++, cmd++) {
		cmd->opcode_channel = pucan_cmd_opcode_channel(dev->ctrl_idx,
							PUCAN_CMD_FILTER_STD);
		cmd->idx = cpu_to_le16(i);
		cmd->mask = cpu_to_le32(mask);
	}

	/* send the command */
	return pcan_usb_fd_send_cmd(dev, cmd);
}

/* set/unset options
 *
 *	onoff	set(1)/unset(0) options
 *	mask	each bit defines a kind of options to set/unset
 */
static int pcan_usb_fd_set_options(struct peak_usb_device *dev,
				   bool onoff, u16 ucan_mask, u16 usb_mask)
{
	struct pcan_ufd_options *cmd = pcan_usb_fd_cmd_buffer(dev);

	cmd->opcode_channel = pucan_cmd_opcode_channel(dev->ctrl_idx,
					(onoff) ? PUCAN_CMD_SET_EN_OPTION :
						  PUCAN_CMD_CLR_DIS_OPTION);

	cmd->ucan_mask = cpu_to_le16(ucan_mask);
	cmd->usb_mask = cpu_to_le16(usb_mask);

	/* send the command */
	return pcan_usb_fd_send_cmd(dev, ++cmd);
}

/* setup LED control */
static int pcan_usb_fd_set_can_led(struct peak_usb_device *dev, u8 led_mode)
{
	struct pcan_ufd_led *cmd = pcan_usb_fd_cmd_buffer(dev);

	cmd->opcode_channel = pucan_cmd_opcode_channel(dev->ctrl_idx,
						       PCAN_UFD_CMD_LED_SET);
	cmd->mode = led_mode;

	/* send the command */
	return pcan_usb_fd_send_cmd(dev, ++cmd);
}

/* set CAN clock domain */
static int pcan_usb_fd_set_clock_domain(struct peak_usb_device *dev,
					u8 clk_mode)
{
	struct pcan_ufd_clock *cmd = pcan_usb_fd_cmd_buffer(dev);

	cmd->opcode_channel = pucan_cmd_opcode_channel(dev->ctrl_idx,
						       PCAN_UFD_CMD_CLK_SET);
	cmd->mode = clk_mode;

	/* send the command */
	return pcan_usb_fd_send_cmd(dev, ++cmd);
}

/* set bittiming for CAN and CAN-FD header */
static int pcan_usb_fd_set_bittiming_slow(struct peak_usb_device *dev,
					  struct can_bittiming *bt)
{
	struct pucan_timing_slow *cmd = pcan_usb_fd_cmd_buffer(dev);

	cmd->opcode_channel = pucan_cmd_opcode_channel(dev->ctrl_idx,
						       PUCAN_CMD_TIMING_SLOW);
	cmd->sjw_t = PUCAN_TSLOW_SJW_T(bt->sjw - 1,
				dev->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES);

	cmd->tseg2 = PUCAN_TSLOW_TSEG2(bt->phase_seg2 - 1);
	cmd->tseg1 = PUCAN_TSLOW_TSEG1(bt->prop_seg + bt->phase_seg1 - 1);
	cmd->brp = cpu_to_le16(PUCAN_TSLOW_BRP(bt->brp - 1));

	cmd->ewl = 96;	/* default */

	/* send the command */
	return pcan_usb_fd_send_cmd(dev, ++cmd);
}

/* set CAN-FD bittiming for data */
static int pcan_usb_fd_set_bittiming_fast(struct peak_usb_device *dev,
					  struct can_bittiming *bt)
{
	struct pucan_timing_fast *cmd = pcan_usb_fd_cmd_buffer(dev);

	cmd->opcode_channel = pucan_cmd_opcode_channel(dev->ctrl_idx,
						       PUCAN_CMD_TIMING_FAST);
	cmd->sjw = PUCAN_TFAST_SJW(bt->sjw - 1);
	cmd->tseg2 = PUCAN_TFAST_TSEG2(bt->phase_seg2 - 1);
	cmd->tseg1 = PUCAN_TFAST_TSEG1(bt->prop_seg + bt->phase_seg1 - 1);
	cmd->brp = cpu_to_le16(PUCAN_TFAST_BRP(bt->brp - 1));

	/* send the command */
	return pcan_usb_fd_send_cmd(dev, ++cmd);
}

/* handle restart but in asynchronously way
 * (uses PCAN-USB Pro code to complete asynchronous request)
 */
static int pcan_usb_fd_restart_async(struct peak_usb_device *dev,
				     struct urb *urb, u8 *buf)
{
	u8 *pc = buf;

	/* build the entire cmds list in the provided buffer, to go back into
	 * operational mode.
	 */
	pc += pcan_usb_fd_build_restart_cmd(dev, pc);

	/* add EOC */
	memset(pc, 0xff, sizeof(struct pucan_command));
	pc += sizeof(struct pucan_command);

	/* complete the URB */
	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, PCAN_USBPRO_EP_CMDOUT),
			  buf, pc - buf,
			  pcan_usb_pro_restart_complete, dev);

	/* and submit it. */
	return usb_submit_urb(urb, GFP_ATOMIC);
}

static int pcan_usb_fd_drv_loaded(struct peak_usb_device *dev, bool loaded)
{
	struct pcan_usb_fd_device *pdev =
			container_of(dev, struct pcan_usb_fd_device, dev);

	pdev->cmd_buffer_addr[0] = 0;
	pdev->cmd_buffer_addr[1] = !!loaded;

	return pcan_usb_pro_send_req(dev,
				PCAN_USBPRO_REQ_FCT,
				PCAN_USBPRO_FCT_DRVLD,
				pdev->cmd_buffer_addr,
				PCAN_USBPRO_FCT_DRVLD_REQ_LEN);
}

static int pcan_usb_fd_decode_canmsg(struct pcan_usb_fd_if *usb_if,
				     struct pucan_msg *rx_msg)
{
	struct pucan_rx_msg *rm = (struct pucan_rx_msg *)rx_msg;
	struct peak_usb_device *dev;
	struct net_device *netdev;
	struct canfd_frame *cfd;
	struct sk_buff *skb;
	const u16 rx_msg_flags = le16_to_cpu(rm->flags);

	if (pucan_msg_get_channel(rm) >= ARRAY_SIZE(usb_if->dev))
		return -ENOMEM;

	dev = usb_if->dev[pucan_msg_get_channel(rm)];
	netdev = dev->netdev;

	if (rx_msg_flags & PUCAN_MSG_EXT_DATA_LEN) {
		/* CANFD frame case */
		skb = alloc_canfd_skb(netdev, &cfd);
		if (!skb)
			return -ENOMEM;

		if (rx_msg_flags & PUCAN_MSG_BITRATE_SWITCH)
			cfd->flags |= CANFD_BRS;

		if (rx_msg_flags & PUCAN_MSG_ERROR_STATE_IND)
			cfd->flags |= CANFD_ESI;

		cfd->len = can_fd_dlc2len(pucan_msg_get_dlc(rm));
	} else {
		/* CAN 2.0 frame case */
		skb = alloc_can_skb(netdev, (struct can_frame **)&cfd);
		if (!skb)
			return -ENOMEM;

		can_frame_set_cc_len((struct can_frame *)cfd,
				     pucan_msg_get_dlc(rm),
				     dev->can.ctrlmode);
	}

	cfd->can_id = le32_to_cpu(rm->can_id);

	if (rx_msg_flags & PUCAN_MSG_EXT_ID)
		cfd->can_id |= CAN_EFF_FLAG;

	if (rx_msg_flags & PUCAN_MSG_RTR)
		cfd->can_id |= CAN_RTR_FLAG;
	else
		memcpy(cfd->data, rm->d, cfd->len);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += cfd->len;

	peak_usb_netif_rx(skb, &usb_if->time_ref, le32_to_cpu(rm->ts_low));

	return 0;
}

/* handle uCAN status message */
static int pcan_usb_fd_decode_status(struct pcan_usb_fd_if *usb_if,
				     struct pucan_msg *rx_msg)
{
	struct pucan_status_msg *sm = (struct pucan_status_msg *)rx_msg;
	struct pcan_usb_fd_device *pdev;
	enum can_state new_state = CAN_STATE_ERROR_ACTIVE;
	enum can_state rx_state, tx_state;
	struct peak_usb_device *dev;
	struct net_device *netdev;
	struct can_frame *cf;
	struct sk_buff *skb;

	if (pucan_stmsg_get_channel(sm) >= ARRAY_SIZE(usb_if->dev))
		return -ENOMEM;

	dev = usb_if->dev[pucan_stmsg_get_channel(sm)];
	pdev = container_of(dev, struct pcan_usb_fd_device, dev);
	netdev = dev->netdev;

	/* nothing should be sent while in BUS_OFF state */
	if (dev->can.state == CAN_STATE_BUS_OFF)
		return 0;

	if (sm->channel_p_w_b & PUCAN_BUS_BUSOFF) {
		new_state = CAN_STATE_BUS_OFF;
	} else if (sm->channel_p_w_b & PUCAN_BUS_PASSIVE) {
		new_state = CAN_STATE_ERROR_PASSIVE;
	} else if (sm->channel_p_w_b & PUCAN_BUS_WARNING) {
		new_state = CAN_STATE_ERROR_WARNING;
	} else {
		/* no error bit (so, no error skb, back to active state) */
		dev->can.state = CAN_STATE_ERROR_ACTIVE;
		pdev->bec.txerr = 0;
		pdev->bec.rxerr = 0;
		return 0;
	}

	/* state hasn't changed */
	if (new_state == dev->can.state)
		return 0;

	/* handle bus state change */
	tx_state = (pdev->bec.txerr >= pdev->bec.rxerr) ? new_state : 0;
	rx_state = (pdev->bec.txerr <= pdev->bec.rxerr) ? new_state : 0;

	/* allocate an skb to store the error frame */
	skb = alloc_can_err_skb(netdev, &cf);
	if (skb)
		can_change_state(netdev, cf, tx_state, rx_state);

	/* things must be done even in case of OOM */
	if (new_state == CAN_STATE_BUS_OFF)
		can_bus_off(netdev);

	if (!skb)
		return -ENOMEM;

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += cf->len;

	peak_usb_netif_rx(skb, &usb_if->time_ref, le32_to_cpu(sm->ts_low));

	return 0;
}

/* handle uCAN error message */
static int pcan_usb_fd_decode_error(struct pcan_usb_fd_if *usb_if,
				    struct pucan_msg *rx_msg)
{
	struct pucan_error_msg *er = (struct pucan_error_msg *)rx_msg;
	struct pcan_usb_fd_device *pdev;
	struct peak_usb_device *dev;

	if (pucan_ermsg_get_channel(er) >= ARRAY_SIZE(usb_if->dev))
		return -EINVAL;

	dev = usb_if->dev[pucan_ermsg_get_channel(er)];
	pdev = container_of(dev, struct pcan_usb_fd_device, dev);

	/* keep a trace of tx and rx error counters for later use */
	pdev->bec.txerr = er->tx_err_cnt;
	pdev->bec.rxerr = er->rx_err_cnt;

	return 0;
}

/* handle uCAN overrun message */
static int pcan_usb_fd_decode_overrun(struct pcan_usb_fd_if *usb_if,
				      struct pucan_msg *rx_msg)
{
	struct pcan_ufd_ovr_msg *ov = (struct pcan_ufd_ovr_msg *)rx_msg;
	struct peak_usb_device *dev;
	struct net_device *netdev;
	struct can_frame *cf;
	struct sk_buff *skb;

	if (pufd_omsg_get_channel(ov) >= ARRAY_SIZE(usb_if->dev))
		return -EINVAL;

	dev = usb_if->dev[pufd_omsg_get_channel(ov)];
	netdev = dev->netdev;

	/* allocate an skb to store the error frame */
	skb = alloc_can_err_skb(netdev, &cf);
	if (!skb)
		return -ENOMEM;

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;

	peak_usb_netif_rx(skb, &usb_if->time_ref, le32_to_cpu(ov->ts_low));

	netdev->stats.rx_over_errors++;
	netdev->stats.rx_errors++;

	return 0;
}

/* handle USB calibration message */
static void pcan_usb_fd_decode_ts(struct pcan_usb_fd_if *usb_if,
				  struct pucan_msg *rx_msg)
{
	struct pcan_ufd_ts_msg *ts = (struct pcan_ufd_ts_msg *)rx_msg;

	/* should wait until clock is stabilized */
	if (usb_if->cm_ignore_count > 0)
		usb_if->cm_ignore_count--;
	else
		peak_usb_set_ts_now(&usb_if->time_ref, le32_to_cpu(ts->ts_low));
}

/* callback for bulk IN urb */
static int pcan_usb_fd_decode_buf(struct peak_usb_device *dev, struct urb *urb)
{
	struct pcan_usb_fd_if *usb_if = pcan_usb_fd_dev_if(dev);
	struct net_device *netdev = dev->netdev;
	struct pucan_msg *rx_msg;
	u8 *msg_ptr, *msg_end;
	int err = 0;

	/* loop reading all the records from the incoming message */
	msg_ptr = urb->transfer_buffer;
	msg_end = urb->transfer_buffer + urb->actual_length;
	for (; msg_ptr < msg_end;) {
		u16 rx_msg_type, rx_msg_size;

		rx_msg = (struct pucan_msg *)msg_ptr;
		if (!rx_msg->size) {
			/* null packet found: end of list */
			break;
		}

		rx_msg_size = le16_to_cpu(rx_msg->size);
		rx_msg_type = le16_to_cpu(rx_msg->type);

		/* check if the record goes out of current packet */
		if (msg_ptr + rx_msg_size > msg_end) {
			netdev_err(netdev,
				   "got frag rec: should inc usb rx buf sze\n");
			err = -EBADMSG;
			break;
		}

		switch (rx_msg_type) {
		case PUCAN_MSG_CAN_RX:
			err = pcan_usb_fd_decode_canmsg(usb_if, rx_msg);
			if (err < 0)
				goto fail;
			break;

		case PCAN_UFD_MSG_CALIBRATION:
			pcan_usb_fd_decode_ts(usb_if, rx_msg);
			break;

		case PUCAN_MSG_ERROR:
			err = pcan_usb_fd_decode_error(usb_if, rx_msg);
			if (err < 0)
				goto fail;
			break;

		case PUCAN_MSG_STATUS:
			err = pcan_usb_fd_decode_status(usb_if, rx_msg);
			if (err < 0)
				goto fail;
			break;

		case PCAN_UFD_MSG_OVERRUN:
			err = pcan_usb_fd_decode_overrun(usb_if, rx_msg);
			if (err < 0)
				goto fail;
			break;

		default:
			netdev_err(netdev,
				   "unhandled msg type 0x%02x (%d): ignored\n",
				   rx_msg_type, rx_msg_type);
			break;
		}

		msg_ptr += rx_msg_size;
	}

fail:
	if (err)
		pcan_dump_mem("received msg",
			      urb->transfer_buffer, urb->actual_length);
	return err;
}

/* CAN/CANFD frames encoding callback */
static int pcan_usb_fd_encode_msg(struct peak_usb_device *dev,
				  struct sk_buff *skb, u8 *obuf, size_t *size)
{
	struct pucan_tx_msg *tx_msg = (struct pucan_tx_msg *)obuf;
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	u16 tx_msg_size, tx_msg_flags;
	u8 dlc;

	if (cfd->len > CANFD_MAX_DLEN)
		return -EINVAL;

	tx_msg_size = ALIGN(sizeof(struct pucan_tx_msg) + cfd->len, 4);
	tx_msg->size = cpu_to_le16(tx_msg_size);
	tx_msg->type = cpu_to_le16(PUCAN_MSG_CAN_TX);

	tx_msg_flags = 0;
	if (cfd->can_id & CAN_EFF_FLAG) {
		tx_msg_flags |= PUCAN_MSG_EXT_ID;
		tx_msg->can_id = cpu_to_le32(cfd->can_id & CAN_EFF_MASK);
	} else {
		tx_msg->can_id = cpu_to_le32(cfd->can_id & CAN_SFF_MASK);
	}

	if (can_is_canfd_skb(skb)) {
		/* considering a CANFD frame */
		dlc = can_fd_len2dlc(cfd->len);

		tx_msg_flags |= PUCAN_MSG_EXT_DATA_LEN;

		if (cfd->flags & CANFD_BRS)
			tx_msg_flags |= PUCAN_MSG_BITRATE_SWITCH;

		if (cfd->flags & CANFD_ESI)
			tx_msg_flags |= PUCAN_MSG_ERROR_STATE_IND;
	} else {
		/* CAND 2.0 frames */
		dlc = can_get_cc_dlc((struct can_frame *)cfd,
				     dev->can.ctrlmode);

		if (cfd->can_id & CAN_RTR_FLAG)
			tx_msg_flags |= PUCAN_MSG_RTR;
	}

	tx_msg->flags = cpu_to_le16(tx_msg_flags);
	tx_msg->channel_dlc = PUCAN_MSG_CHANNEL_DLC(dev->ctrl_idx, dlc);
	memcpy(tx_msg->d, cfd->data, cfd->len);

	/* add null size message to tag the end (messages are 32-bits aligned)
	 */
	tx_msg = (struct pucan_tx_msg *)(obuf + tx_msg_size);

	tx_msg->size = 0;

	/* set the whole size of the USB packet to send */
	*size = tx_msg_size + sizeof(u32);

	return 0;
}

/* start the interface (last chance before set bus on) */
static int pcan_usb_fd_start(struct peak_usb_device *dev)
{
	struct pcan_usb_fd_device *pdev =
			container_of(dev, struct pcan_usb_fd_device, dev);
	int err;

	/* set filter mode: all acceptance */
	err = pcan_usb_fd_set_filter_std(dev, -1, 0xffffffff);
	if (err)
		return err;

	/* opening first device: */
	if (pdev->usb_if->dev_opened_count == 0) {
		/* reset time_ref */
		peak_usb_init_time_ref(&pdev->usb_if->time_ref,
				       &pcan_usb_pro_fd);

		/* enable USB calibration messages */
		err = pcan_usb_fd_set_options(dev, 1,
					      PUCAN_OPTION_ERROR,
					      PCAN_UFD_FLTEXT_CALIBRATION);
	}

	pdev->usb_if->dev_opened_count++;

	/* reset cached error counters */
	pdev->bec.txerr = 0;
	pdev->bec.rxerr = 0;

	return err;
}

/* socket callback used to copy berr counters values received through USB */
static int pcan_usb_fd_get_berr_counter(const struct net_device *netdev,
					struct can_berr_counter *bec)
{
	struct peak_usb_device *dev = netdev_priv(netdev);
	struct pcan_usb_fd_device *pdev =
			container_of(dev, struct pcan_usb_fd_device, dev);

	*bec = pdev->bec;

	/* must return 0 */
	return 0;
}

/* stop interface (last chance before set bus off) */
static int pcan_usb_fd_stop(struct peak_usb_device *dev)
{
	struct pcan_usb_fd_device *pdev =
			container_of(dev, struct pcan_usb_fd_device, dev);

	/* turn off special msgs for that interface if no other dev opened */
	if (pdev->usb_if->dev_opened_count == 1)
		pcan_usb_fd_set_options(dev, 0,
					PUCAN_OPTION_ERROR,
					PCAN_UFD_FLTEXT_CALIBRATION);
	pdev->usb_if->dev_opened_count--;

	return 0;
}

/* called when probing, to initialize a device object */
static int pcan_usb_fd_init(struct peak_usb_device *dev)
{
	struct pcan_usb_fd_device *pdev =
			container_of(dev, struct pcan_usb_fd_device, dev);
	int i, err = -ENOMEM;

	/* do this for 1st channel only */
	if (!dev->prev_siblings) {
		/* allocate netdevices common structure attached to first one */
		pdev->usb_if = kzalloc(sizeof(*pdev->usb_if), GFP_KERNEL);
		if (!pdev->usb_if)
			goto err_out;

		/* allocate command buffer once for all for the interface */
		pdev->cmd_buffer_addr = kzalloc(PCAN_UFD_CMD_BUFFER_SIZE,
						GFP_KERNEL);
		if (!pdev->cmd_buffer_addr)
			goto err_out_1;

		/* number of ts msgs to ignore before taking one into account */
		pdev->usb_if->cm_ignore_count = 5;

		err = pcan_usb_pro_send_req(dev, PCAN_USBPRO_REQ_INFO,
					    PCAN_USBPRO_INFO_FW,
					    &pdev->usb_if->fw_info,
					    sizeof(pdev->usb_if->fw_info));
		if (err) {
			dev_err(dev->netdev->dev.parent,
				"unable to read %s firmware info (err %d)\n",
				dev->adapter->name, err);
			goto err_out_2;
		}

		/* explicit use of dev_xxx() instead of netdev_xxx() here:
		 * information displayed are related to the device itself, not
		 * to the canx (channel) device.
		 */
		dev_info(dev->netdev->dev.parent,
			 "PEAK-System %s v%u fw v%u.%u.%u (%u channels)\n",
			 dev->adapter->name, pdev->usb_if->fw_info.hw_version,
			 pdev->usb_if->fw_info.fw_version[0],
			 pdev->usb_if->fw_info.fw_version[1],
			 pdev->usb_if->fw_info.fw_version[2],
			 dev->adapter->ctrl_count);

		/* check for ability to switch between ISO/non-ISO modes */
		if (pdev->usb_if->fw_info.fw_version[0] >= 2) {
			/* firmware >= 2.x supports ISO/non-ISO switching */
			dev->can.ctrlmode_supported |= CAN_CTRLMODE_FD_NON_ISO;
		} else {
			/* firmware < 2.x only supports fixed(!) non-ISO */
			dev->can.ctrlmode |= CAN_CTRLMODE_FD_NON_ISO;
		}

		/* tell the hardware the can driver is running */
		err = pcan_usb_fd_drv_loaded(dev, 1);
		if (err) {
			dev_err(dev->netdev->dev.parent,
				"unable to tell %s driver is loaded (err %d)\n",
				dev->adapter->name, err);
			goto err_out_2;
		}
	} else {
		/* otherwise, simply copy previous sibling's values */
		struct pcan_usb_fd_device *ppdev =
			container_of(dev->prev_siblings,
				     struct pcan_usb_fd_device, dev);

		pdev->usb_if = ppdev->usb_if;
		pdev->cmd_buffer_addr = ppdev->cmd_buffer_addr;

		/* do a copy of the ctrlmode[_supported] too */
		dev->can.ctrlmode = ppdev->dev.can.ctrlmode;
		dev->can.ctrlmode_supported = ppdev->dev.can.ctrlmode_supported;
	}

	pdev->usb_if->dev[dev->ctrl_idx] = dev;
	dev->device_number =
		le32_to_cpu(pdev->usb_if->fw_info.dev_id[dev->ctrl_idx]);

	/* set clock domain */
	for (i = 0; i < ARRAY_SIZE(pcan_usb_fd_clk_freq); i++)
		if (dev->adapter->clock.freq == pcan_usb_fd_clk_freq[i])
			break;

	if (i >= ARRAY_SIZE(pcan_usb_fd_clk_freq)) {
		dev_warn(dev->netdev->dev.parent,
			 "incompatible clock frequencies\n");
		err = -EINVAL;
		goto err_out_2;
	}

	pcan_usb_fd_set_clock_domain(dev, i);

	/* set LED in default state (end of init phase) */
	pcan_usb_fd_set_can_led(dev, PCAN_UFD_LED_DEF);

	return 0;

err_out_2:
	kfree(pdev->cmd_buffer_addr);
err_out_1:
	kfree(pdev->usb_if);
err_out:
	return err;
}

/* called when driver module is being unloaded */
static void pcan_usb_fd_exit(struct peak_usb_device *dev)
{
	struct pcan_usb_fd_device *pdev =
			container_of(dev, struct pcan_usb_fd_device, dev);

	/* when rmmod called before unplug and if down, should reset things
	 * before leaving
	 */
	if (dev->can.state != CAN_STATE_STOPPED) {
		/* set bus off on the corresponding channel */
		pcan_usb_fd_set_bus(dev, 0);
	}

	/* switch off corresponding CAN LEDs */
	pcan_usb_fd_set_can_led(dev, PCAN_UFD_LED_OFF);

	/* if channel #0 (only) */
	if (dev->ctrl_idx == 0) {
		/* turn off calibration message if any device were opened */
		if (pdev->usb_if->dev_opened_count > 0)
			pcan_usb_fd_set_options(dev, 0,
						PUCAN_OPTION_ERROR,
						PCAN_UFD_FLTEXT_CALIBRATION);

		/* tell USB adapter that the driver is being unloaded */
		pcan_usb_fd_drv_loaded(dev, 0);
	}
}

/* called when the USB adapter is unplugged */
static void pcan_usb_fd_free(struct peak_usb_device *dev)
{
	/* last device: can free shared objects now */
	if (!dev->prev_siblings && !dev->next_siblings) {
		struct pcan_usb_fd_device *pdev =
			container_of(dev, struct pcan_usb_fd_device, dev);

		/* free commands buffer */
		kfree(pdev->cmd_buffer_addr);

		/* free usb interface object */
		kfree(pdev->usb_if);
	}
}

/* describes the PCAN-USB FD adapter */
static const struct can_bittiming_const pcan_usb_fd_const = {
	.name = "pcan_usb_fd",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TSLOW_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TSLOW_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TSLOW_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TSLOW_BRP_BITS),
	.brp_inc = 1,
};

static const struct can_bittiming_const pcan_usb_fd_data_const = {
	.name = "pcan_usb_fd",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TFAST_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TFAST_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TFAST_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TFAST_BRP_BITS),
	.brp_inc = 1,
};

const struct peak_usb_adapter pcan_usb_fd = {
	.name = "PCAN-USB FD",
	.device_id = PCAN_USBFD_PRODUCT_ID,
	.ctrl_count = PCAN_USBFD_CHANNEL_COUNT,
	.ctrlmode_supported = CAN_CTRLMODE_FD |
			CAN_CTRLMODE_3_SAMPLES | CAN_CTRLMODE_LISTENONLY |
			CAN_CTRLMODE_CC_LEN8_DLC,
	.clock = {
		.freq = PCAN_UFD_CRYSTAL_HZ,
	},
	.bittiming_const = &pcan_usb_fd_const,
	.data_bittiming_const = &pcan_usb_fd_data_const,

	/* size of device private data */
	.sizeof_dev_private = sizeof(struct pcan_usb_fd_device),

	/* timestamps usage */
	.ts_used_bits = 32,
	.ts_period = 1000000, /* calibration period in ts. */
	.us_per_ts_scale = 1, /* us = (ts * scale) >> shift */
	.us_per_ts_shift = 0,

	/* give here messages in/out endpoints */
	.ep_msg_in = PCAN_USBPRO_EP_MSGIN,
	.ep_msg_out = {PCAN_USBPRO_EP_MSGOUT_0},

	/* size of rx/tx usb buffers */
	.rx_buffer_size = PCAN_UFD_RX_BUFFER_SIZE,
	.tx_buffer_size = PCAN_UFD_TX_BUFFER_SIZE,

	/* device callbacks */
	.intf_probe = pcan_usb_pro_probe,	/* same as PCAN-USB Pro */
	.dev_init = pcan_usb_fd_init,

	.dev_exit = pcan_usb_fd_exit,
	.dev_free = pcan_usb_fd_free,
	.dev_set_bus = pcan_usb_fd_set_bus,
	.dev_set_bittiming = pcan_usb_fd_set_bittiming_slow,
	.dev_set_data_bittiming = pcan_usb_fd_set_bittiming_fast,
	.dev_decode_buf = pcan_usb_fd_decode_buf,
	.dev_start = pcan_usb_fd_start,
	.dev_stop = pcan_usb_fd_stop,
	.dev_restart_async = pcan_usb_fd_restart_async,
	.dev_encode_msg = pcan_usb_fd_encode_msg,

	.do_get_berr_counter = pcan_usb_fd_get_berr_counter,
};

/* describes the PCAN-CHIP USB */
static const struct can_bittiming_const pcan_usb_chip_const = {
	.name = "pcan_chip_usb",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TSLOW_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TSLOW_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TSLOW_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TSLOW_BRP_BITS),
	.brp_inc = 1,
};

static const struct can_bittiming_const pcan_usb_chip_data_const = {
	.name = "pcan_chip_usb",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TFAST_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TFAST_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TFAST_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TFAST_BRP_BITS),
	.brp_inc = 1,
};

const struct peak_usb_adapter pcan_usb_chip = {
	.name = "PCAN-Chip USB",
	.device_id = PCAN_USBCHIP_PRODUCT_ID,
	.ctrl_count = PCAN_USBFD_CHANNEL_COUNT,
	.ctrlmode_supported = CAN_CTRLMODE_FD |
		CAN_CTRLMODE_3_SAMPLES | CAN_CTRLMODE_LISTENONLY |
		CAN_CTRLMODE_CC_LEN8_DLC,
	.clock = {
		.freq = PCAN_UFD_CRYSTAL_HZ,
	},
	.bittiming_const = &pcan_usb_chip_const,
	.data_bittiming_const = &pcan_usb_chip_data_const,

	/* size of device private data */
	.sizeof_dev_private = sizeof(struct pcan_usb_fd_device),

	/* timestamps usage */
	.ts_used_bits = 32,
	.ts_period = 1000000, /* calibration period in ts. */
	.us_per_ts_scale = 1, /* us = (ts * scale) >> shift */
	.us_per_ts_shift = 0,

	/* give here messages in/out endpoints */
	.ep_msg_in = PCAN_USBPRO_EP_MSGIN,
	.ep_msg_out = {PCAN_USBPRO_EP_MSGOUT_0},

	/* size of rx/tx usb buffers */
	.rx_buffer_size = PCAN_UFD_RX_BUFFER_SIZE,
	.tx_buffer_size = PCAN_UFD_TX_BUFFER_SIZE,

	/* device callbacks */
	.intf_probe = pcan_usb_pro_probe,	/* same as PCAN-USB Pro */
	.dev_init = pcan_usb_fd_init,

	.dev_exit = pcan_usb_fd_exit,
	.dev_free = pcan_usb_fd_free,
	.dev_set_bus = pcan_usb_fd_set_bus,
	.dev_set_bittiming = pcan_usb_fd_set_bittiming_slow,
	.dev_set_data_bittiming = pcan_usb_fd_set_bittiming_fast,
	.dev_decode_buf = pcan_usb_fd_decode_buf,
	.dev_start = pcan_usb_fd_start,
	.dev_stop = pcan_usb_fd_stop,
	.dev_restart_async = pcan_usb_fd_restart_async,
	.dev_encode_msg = pcan_usb_fd_encode_msg,

	.do_get_berr_counter = pcan_usb_fd_get_berr_counter,
};

/* describes the PCAN-USB Pro FD adapter */
static const struct can_bittiming_const pcan_usb_pro_fd_const = {
	.name = "pcan_usb_pro_fd",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TSLOW_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TSLOW_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TSLOW_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TSLOW_BRP_BITS),
	.brp_inc = 1,
};

static const struct can_bittiming_const pcan_usb_pro_fd_data_const = {
	.name = "pcan_usb_pro_fd",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TFAST_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TFAST_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TFAST_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TFAST_BRP_BITS),
	.brp_inc = 1,
};

const struct peak_usb_adapter pcan_usb_pro_fd = {
	.name = "PCAN-USB Pro FD",
	.device_id = PCAN_USBPROFD_PRODUCT_ID,
	.ctrl_count = PCAN_USBPROFD_CHANNEL_COUNT,
	.ctrlmode_supported = CAN_CTRLMODE_FD |
			CAN_CTRLMODE_3_SAMPLES | CAN_CTRLMODE_LISTENONLY |
			CAN_CTRLMODE_CC_LEN8_DLC,
	.clock = {
		.freq = PCAN_UFD_CRYSTAL_HZ,
	},
	.bittiming_const = &pcan_usb_pro_fd_const,
	.data_bittiming_const = &pcan_usb_pro_fd_data_const,

	/* size of device private data */
	.sizeof_dev_private = sizeof(struct pcan_usb_fd_device),

	/* timestamps usage */
	.ts_used_bits = 32,
	.ts_period = 1000000, /* calibration period in ts. */
	.us_per_ts_scale = 1, /* us = (ts * scale) >> shift */
	.us_per_ts_shift = 0,

	/* give here messages in/out endpoints */
	.ep_msg_in = PCAN_USBPRO_EP_MSGIN,
	.ep_msg_out = {PCAN_USBPRO_EP_MSGOUT_0, PCAN_USBPRO_EP_MSGOUT_1},

	/* size of rx/tx usb buffers */
	.rx_buffer_size = PCAN_UFD_RX_BUFFER_SIZE,
	.tx_buffer_size = PCAN_UFD_TX_BUFFER_SIZE,

	/* device callbacks */
	.intf_probe = pcan_usb_pro_probe,	/* same as PCAN-USB Pro */
	.dev_init = pcan_usb_fd_init,

	.dev_exit = pcan_usb_fd_exit,
	.dev_free = pcan_usb_fd_free,
	.dev_set_bus = pcan_usb_fd_set_bus,
	.dev_set_bittiming = pcan_usb_fd_set_bittiming_slow,
	.dev_set_data_bittiming = pcan_usb_fd_set_bittiming_fast,
	.dev_decode_buf = pcan_usb_fd_decode_buf,
	.dev_start = pcan_usb_fd_start,
	.dev_stop = pcan_usb_fd_stop,
	.dev_restart_async = pcan_usb_fd_restart_async,
	.dev_encode_msg = pcan_usb_fd_encode_msg,

	.do_get_berr_counter = pcan_usb_fd_get_berr_counter,
};

/* describes the PCAN-USB X6 adapter */
static const struct can_bittiming_const pcan_usb_x6_const = {
	.name = "pcan_usb_x6",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TSLOW_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TSLOW_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TSLOW_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TSLOW_BRP_BITS),
	.brp_inc = 1,
};

static const struct can_bittiming_const pcan_usb_x6_data_const = {
	.name = "pcan_usb_x6",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TFAST_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TFAST_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TFAST_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TFAST_BRP_BITS),
	.brp_inc = 1,
};

const struct peak_usb_adapter pcan_usb_x6 = {
	.name = "PCAN-USB X6",
	.device_id = PCAN_USBX6_PRODUCT_ID,
	.ctrl_count = PCAN_USBPROFD_CHANNEL_COUNT,
	.ctrlmode_supported = CAN_CTRLMODE_FD |
			CAN_CTRLMODE_3_SAMPLES | CAN_CTRLMODE_LISTENONLY |
			CAN_CTRLMODE_CC_LEN8_DLC,
	.clock = {
		.freq = PCAN_UFD_CRYSTAL_HZ,
	},
	.bittiming_const = &pcan_usb_x6_const,
	.data_bittiming_const = &pcan_usb_x6_data_const,

	/* size of device private data */
	.sizeof_dev_private = sizeof(struct pcan_usb_fd_device),

	/* timestamps usage */
	.ts_used_bits = 32,
	.ts_period = 1000000, /* calibration period in ts. */
	.us_per_ts_scale = 1, /* us = (ts * scale) >> shift */
	.us_per_ts_shift = 0,

	/* give here messages in/out endpoints */
	.ep_msg_in = PCAN_USBPRO_EP_MSGIN,
	.ep_msg_out = {PCAN_USBPRO_EP_MSGOUT_0, PCAN_USBPRO_EP_MSGOUT_1},

	/* size of rx/tx usb buffers */
	.rx_buffer_size = PCAN_UFD_RX_BUFFER_SIZE,
	.tx_buffer_size = PCAN_UFD_TX_BUFFER_SIZE,

	/* device callbacks */
	.intf_probe = pcan_usb_pro_probe,	/* same as PCAN-USB Pro */
	.dev_init = pcan_usb_fd_init,

	.dev_exit = pcan_usb_fd_exit,
	.dev_free = pcan_usb_fd_free,
	.dev_set_bus = pcan_usb_fd_set_bus,
	.dev_set_bittiming = pcan_usb_fd_set_bittiming_slow,
	.dev_set_data_bittiming = pcan_usb_fd_set_bittiming_fast,
	.dev_decode_buf = pcan_usb_fd_decode_buf,
	.dev_start = pcan_usb_fd_start,
	.dev_stop = pcan_usb_fd_stop,
	.dev_restart_async = pcan_usb_fd_restart_async,
	.dev_encode_msg = pcan_usb_fd_encode_msg,

	.do_get_berr_counter = pcan_usb_fd_get_berr_counter,
};
