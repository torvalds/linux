/*
 * CAN driver for PEAK System PCAN-USB Pro adapter
 * Derived from the PCAN project file driver/src/pcan_usbpro.c
 *
 * Copyright (C) 2003-2011 PEAK System-Technik GmbH
 * Copyright (C) 2011-2012 Stephane Grosjean <s.grosjean@peak-system.com>
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
#include <linux/netdevice.h>
#include <linux/usb.h>
#include <linux/module.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

#include "pcan_usb_core.h"
#include "pcan_usb_pro.h"

MODULE_SUPPORTED_DEVICE("PEAK-System PCAN-USB Pro adapter");

#define PCAN_USBPRO_CHANNEL_COUNT	2

/* PCAN-USB Pro adapter internal clock (MHz) */
#define PCAN_USBPRO_CRYSTAL_HZ		56000000

/* PCAN-USB Pro command timeout (ms.) */
#define PCAN_USBPRO_COMMAND_TIMEOUT	1000

/* PCAN-USB Pro rx/tx buffers size */
#define PCAN_USBPRO_RX_BUFFER_SIZE	1024
#define PCAN_USBPRO_TX_BUFFER_SIZE	64

#define PCAN_USBPRO_MSG_HEADER_LEN	4

/* some commands responses need to be re-submitted */
#define PCAN_USBPRO_RSP_SUBMIT_MAX	2

#define PCAN_USBPRO_RTR			0x01
#define PCAN_USBPRO_EXT			0x02

#define PCAN_USBPRO_CMD_BUFFER_SIZE	512

/* handle device specific info used by the netdevices */
struct pcan_usb_pro_interface {
	struct peak_usb_device *dev[PCAN_USBPRO_CHANNEL_COUNT];
	struct peak_time_ref time_ref;
	int cm_ignore_count;
	int dev_opened_count;
};

/* device information */
struct pcan_usb_pro_device {
	struct peak_usb_device dev;
	struct pcan_usb_pro_interface *usb_if;
	u32 cached_ccbt;
};

/* internal structure used to handle messages sent to bulk urb */
struct pcan_usb_pro_msg {
	u8 *rec_ptr;
	int rec_buffer_size;
	int rec_buffer_len;
	union {
		__le16 *rec_cnt_rd;
		__le32 *rec_cnt;
		u8 *rec_buffer;
	} u;
};

/* records sizes table indexed on message id. (8-bits value) */
static u16 pcan_usb_pro_sizeof_rec[256] = {
	[PCAN_USBPRO_SETBTR] = sizeof(struct pcan_usb_pro_btr),
	[PCAN_USBPRO_SETBUSACT] = sizeof(struct pcan_usb_pro_busact),
	[PCAN_USBPRO_SETSILENT] = sizeof(struct pcan_usb_pro_silent),
	[PCAN_USBPRO_SETFILTR] = sizeof(struct pcan_usb_pro_filter),
	[PCAN_USBPRO_SETTS] = sizeof(struct pcan_usb_pro_setts),
	[PCAN_USBPRO_GETDEVID] = sizeof(struct pcan_usb_pro_devid),
	[PCAN_USBPRO_SETLED] = sizeof(struct pcan_usb_pro_setled),
	[PCAN_USBPRO_RXMSG8] = sizeof(struct pcan_usb_pro_rxmsg),
	[PCAN_USBPRO_RXMSG4] = sizeof(struct pcan_usb_pro_rxmsg) - 4,
	[PCAN_USBPRO_RXMSG0] = sizeof(struct pcan_usb_pro_rxmsg) - 8,
	[PCAN_USBPRO_RXRTR] = sizeof(struct pcan_usb_pro_rxmsg) - 8,
	[PCAN_USBPRO_RXSTATUS] = sizeof(struct pcan_usb_pro_rxstatus),
	[PCAN_USBPRO_RXTS] = sizeof(struct pcan_usb_pro_rxts),
	[PCAN_USBPRO_TXMSG8] = sizeof(struct pcan_usb_pro_txmsg),
	[PCAN_USBPRO_TXMSG4] = sizeof(struct pcan_usb_pro_txmsg) - 4,
	[PCAN_USBPRO_TXMSG0] = sizeof(struct pcan_usb_pro_txmsg) - 8,
};

/*
 * initialize PCAN-USB Pro message data structure
 */
static u8 *pcan_msg_init(struct pcan_usb_pro_msg *pm, void *buffer_addr,
			 int buffer_size)
{
	if (buffer_size < PCAN_USBPRO_MSG_HEADER_LEN)
		return NULL;

	pm->u.rec_buffer = (u8 *)buffer_addr;
	pm->rec_buffer_size = pm->rec_buffer_len = buffer_size;
	pm->rec_ptr = pm->u.rec_buffer + PCAN_USBPRO_MSG_HEADER_LEN;

	return pm->rec_ptr;
}

static u8 *pcan_msg_init_empty(struct pcan_usb_pro_msg *pm,
			       void *buffer_addr, int buffer_size)
{
	u8 *pr = pcan_msg_init(pm, buffer_addr, buffer_size);

	if (pr) {
		pm->rec_buffer_len = PCAN_USBPRO_MSG_HEADER_LEN;
		*pm->u.rec_cnt = 0;
	}
	return pr;
}

/*
 * add one record to a message being built
 */
static int pcan_msg_add_rec(struct pcan_usb_pro_msg *pm, u8 id, ...)
{
	int len, i;
	u8 *pc;
	va_list ap;

	va_start(ap, id);

	pc = pm->rec_ptr + 1;

	i = 0;
	switch (id) {
	case PCAN_USBPRO_TXMSG8:
		i += 4;
	case PCAN_USBPRO_TXMSG4:
		i += 4;
	case PCAN_USBPRO_TXMSG0:
		*pc++ = va_arg(ap, int);
		*pc++ = va_arg(ap, int);
		*pc++ = va_arg(ap, int);
		*(__le32 *)pc = cpu_to_le32(va_arg(ap, u32));
		pc += 4;
		memcpy(pc, va_arg(ap, int *), i);
		pc += i;
		break;

	case PCAN_USBPRO_SETBTR:
	case PCAN_USBPRO_GETDEVID:
		*pc++ = va_arg(ap, int);
		pc += 2;
		*(__le32 *)pc = cpu_to_le32(va_arg(ap, u32));
		pc += 4;
		break;

	case PCAN_USBPRO_SETFILTR:
	case PCAN_USBPRO_SETBUSACT:
	case PCAN_USBPRO_SETSILENT:
		*pc++ = va_arg(ap, int);
		*(__le16 *)pc = cpu_to_le16(va_arg(ap, int));
		pc += 2;
		break;

	case PCAN_USBPRO_SETLED:
		*pc++ = va_arg(ap, int);
		*(__le16 *)pc = cpu_to_le16(va_arg(ap, int));
		pc += 2;
		*(__le32 *)pc = cpu_to_le32(va_arg(ap, u32));
		pc += 4;
		break;

	case PCAN_USBPRO_SETTS:
		pc++;
		*(__le16 *)pc = cpu_to_le16(va_arg(ap, int));
		pc += 2;
		break;

	default:
		pr_err("%s: %s(): unknown data type %02Xh (%d)\n",
			PCAN_USB_DRIVER_NAME, __func__, id, id);
		pc--;
		break;
	}

	len = pc - pm->rec_ptr;
	if (len > 0) {
		*pm->u.rec_cnt = cpu_to_le32(le32_to_cpu(*pm->u.rec_cnt) + 1);
		*pm->rec_ptr = id;

		pm->rec_ptr = pc;
		pm->rec_buffer_len += len;
	}

	va_end(ap);

	return len;
}

/*
 * send PCAN-USB Pro command synchronously
 */
static int pcan_usb_pro_send_cmd(struct peak_usb_device *dev,
				 struct pcan_usb_pro_msg *pum)
{
	int actual_length;
	int err;

	/* usb device unregistered? */
	if (!(dev->state & PCAN_USB_STATE_CONNECTED))
		return 0;

	err = usb_bulk_msg(dev->udev,
		usb_sndbulkpipe(dev->udev, PCAN_USBPRO_EP_CMDOUT),
		pum->u.rec_buffer, pum->rec_buffer_len,
		&actual_length, PCAN_USBPRO_COMMAND_TIMEOUT);
	if (err)
		netdev_err(dev->netdev, "sending command failure: %d\n", err);

	return err;
}

/*
 * wait for PCAN-USB Pro command response
 */
static int pcan_usb_pro_wait_rsp(struct peak_usb_device *dev,
				 struct pcan_usb_pro_msg *pum)
{
	u8 req_data_type, req_channel;
	int actual_length;
	int i, err = 0;

	/* usb device unregistered? */
	if (!(dev->state & PCAN_USB_STATE_CONNECTED))
		return 0;

	req_data_type = pum->u.rec_buffer[4];
	req_channel = pum->u.rec_buffer[5];

	*pum->u.rec_cnt = 0;
	for (i = 0; !err && i < PCAN_USBPRO_RSP_SUBMIT_MAX; i++) {
		struct pcan_usb_pro_msg rsp;
		union pcan_usb_pro_rec *pr;
		u32 r, rec_cnt;
		u16 rec_len;
		u8 *pc;

		err = usb_bulk_msg(dev->udev,
			usb_rcvbulkpipe(dev->udev, PCAN_USBPRO_EP_CMDIN),
			pum->u.rec_buffer, pum->rec_buffer_len,
			&actual_length, PCAN_USBPRO_COMMAND_TIMEOUT);
		if (err) {
			netdev_err(dev->netdev, "waiting rsp error %d\n", err);
			break;
		}

		if (actual_length == 0)
			continue;

		err = -EBADMSG;
		if (actual_length < PCAN_USBPRO_MSG_HEADER_LEN) {
			netdev_err(dev->netdev,
				   "got abnormal too small rsp (len=%d)\n",
				   actual_length);
			break;
		}

		pc = pcan_msg_init(&rsp, pum->u.rec_buffer,
			actual_length);

		rec_cnt = le32_to_cpu(*rsp.u.rec_cnt);

		/* loop on records stored into message */
		for (r = 0; r < rec_cnt; r++) {
			pr = (union pcan_usb_pro_rec *)pc;
			rec_len = pcan_usb_pro_sizeof_rec[pr->data_type];
			if (!rec_len) {
				netdev_err(dev->netdev,
					   "got unprocessed record in msg\n");
				pcan_dump_mem("rcvd rsp msg", pum->u.rec_buffer,
					      actual_length);
				break;
			}

			/* check if response corresponds to request */
			if (pr->data_type != req_data_type)
				netdev_err(dev->netdev,
					   "got unwanted rsp %xh: ignored\n",
					   pr->data_type);

			/* check if channel in response corresponds too */
			else if ((req_channel != 0xff) && \
				(pr->bus_act.channel != req_channel))
				netdev_err(dev->netdev,
					"got rsp %xh but on chan%u: ignored\n",
					req_data_type, pr->bus_act.channel);

			/* got the response */
			else
				return 0;

			/* otherwise, go on with next record in message */
			pc += rec_len;
		}
	}

	return (i >= PCAN_USBPRO_RSP_SUBMIT_MAX) ? -ERANGE : err;
}

int pcan_usb_pro_send_req(struct peak_usb_device *dev, int req_id,
			  int req_value, void *req_addr, int req_size)
{
	int err;
	u8 req_type;
	unsigned int p;

	/* usb device unregistered? */
	if (!(dev->state & PCAN_USB_STATE_CONNECTED))
		return 0;

	req_type = USB_TYPE_VENDOR | USB_RECIP_OTHER;

	switch (req_id) {
	case PCAN_USBPRO_REQ_FCT:
		p = usb_sndctrlpipe(dev->udev, 0);
		break;

	default:
		p = usb_rcvctrlpipe(dev->udev, 0);
		req_type |= USB_DIR_IN;
		memset(req_addr, '\0', req_size);
		break;
	}

	err = usb_control_msg(dev->udev, p, req_id, req_type, req_value, 0,
			      req_addr, req_size, 2 * USB_CTRL_GET_TIMEOUT);
	if (err < 0) {
		netdev_info(dev->netdev,
			    "unable to request usb[type=%d value=%d] err=%d\n",
			    req_id, req_value, err);
		return err;
	}

	return 0;
}

static int pcan_usb_pro_set_ts(struct peak_usb_device *dev, u16 onoff)
{
	struct pcan_usb_pro_msg um;

	pcan_msg_init_empty(&um, dev->cmd_buf, PCAN_USB_MAX_CMD_LEN);
	pcan_msg_add_rec(&um, PCAN_USBPRO_SETTS, onoff);

	return pcan_usb_pro_send_cmd(dev, &um);
}

static int pcan_usb_pro_set_bitrate(struct peak_usb_device *dev, u32 ccbt)
{
	struct pcan_usb_pro_device *pdev =
			container_of(dev, struct pcan_usb_pro_device, dev);
	struct pcan_usb_pro_msg um;

	pcan_msg_init_empty(&um, dev->cmd_buf, PCAN_USB_MAX_CMD_LEN);
	pcan_msg_add_rec(&um, PCAN_USBPRO_SETBTR, dev->ctrl_idx, ccbt);

	/* cache the CCBT value to reuse it before next buson */
	pdev->cached_ccbt = ccbt;

	return pcan_usb_pro_send_cmd(dev, &um);
}

static int pcan_usb_pro_set_bus(struct peak_usb_device *dev, u8 onoff)
{
	struct pcan_usb_pro_msg um;

	/* if bus=on, be sure the bitrate being set before! */
	if (onoff) {
		struct pcan_usb_pro_device *pdev =
			     container_of(dev, struct pcan_usb_pro_device, dev);

		pcan_usb_pro_set_bitrate(dev, pdev->cached_ccbt);
	}

	pcan_msg_init_empty(&um, dev->cmd_buf, PCAN_USB_MAX_CMD_LEN);
	pcan_msg_add_rec(&um, PCAN_USBPRO_SETBUSACT, dev->ctrl_idx, onoff);

	return pcan_usb_pro_send_cmd(dev, &um);
}

static int pcan_usb_pro_set_silent(struct peak_usb_device *dev, u8 onoff)
{
	struct pcan_usb_pro_msg um;

	pcan_msg_init_empty(&um, dev->cmd_buf, PCAN_USB_MAX_CMD_LEN);
	pcan_msg_add_rec(&um, PCAN_USBPRO_SETSILENT, dev->ctrl_idx, onoff);

	return pcan_usb_pro_send_cmd(dev, &um);
}

static int pcan_usb_pro_set_filter(struct peak_usb_device *dev, u16 filter_mode)
{
	struct pcan_usb_pro_msg um;

	pcan_msg_init_empty(&um, dev->cmd_buf, PCAN_USB_MAX_CMD_LEN);
	pcan_msg_add_rec(&um, PCAN_USBPRO_SETFILTR, dev->ctrl_idx, filter_mode);

	return pcan_usb_pro_send_cmd(dev, &um);
}

static int pcan_usb_pro_set_led(struct peak_usb_device *dev, u8 mode,
				u32 timeout)
{
	struct pcan_usb_pro_msg um;

	pcan_msg_init_empty(&um, dev->cmd_buf, PCAN_USB_MAX_CMD_LEN);
	pcan_msg_add_rec(&um, PCAN_USBPRO_SETLED, dev->ctrl_idx, mode, timeout);

	return pcan_usb_pro_send_cmd(dev, &um);
}

static int pcan_usb_pro_get_device_id(struct peak_usb_device *dev,
				      u32 *device_id)
{
	struct pcan_usb_pro_devid *pdn;
	struct pcan_usb_pro_msg um;
	int err;
	u8 *pc;

	pc = pcan_msg_init_empty(&um, dev->cmd_buf, PCAN_USB_MAX_CMD_LEN);
	pcan_msg_add_rec(&um, PCAN_USBPRO_GETDEVID, dev->ctrl_idx);

	err =  pcan_usb_pro_send_cmd(dev, &um);
	if (err)
		return err;

	err = pcan_usb_pro_wait_rsp(dev, &um);
	if (err)
		return err;

	pdn = (struct pcan_usb_pro_devid *)pc;
	if (device_id)
		*device_id = le32_to_cpu(pdn->serial_num);

	return err;
}

static int pcan_usb_pro_set_bittiming(struct peak_usb_device *dev,
				      struct can_bittiming *bt)
{
	u32 ccbt;

	ccbt = (dev->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES) ? 0x00800000 : 0;
	ccbt |= (bt->sjw - 1) << 24;
	ccbt |= (bt->phase_seg2 - 1) << 20;
	ccbt |= (bt->prop_seg + bt->phase_seg1 - 1) << 16; /* = tseg1 */
	ccbt |= bt->brp - 1;

	netdev_info(dev->netdev, "setting ccbt=0x%08x\n", ccbt);

	return pcan_usb_pro_set_bitrate(dev, ccbt);
}

void pcan_usb_pro_restart_complete(struct urb *urb)
{
	/* can delete usb resources */
	peak_usb_async_complete(urb);

	/* notify candev and netdev */
	peak_usb_restart_complete(urb->context);
}

/*
 * handle restart but in asynchronously way
 */
static int pcan_usb_pro_restart_async(struct peak_usb_device *dev,
				      struct urb *urb, u8 *buf)
{
	struct pcan_usb_pro_msg um;

	pcan_msg_init_empty(&um, buf, PCAN_USB_MAX_CMD_LEN);
	pcan_msg_add_rec(&um, PCAN_USBPRO_SETBUSACT, dev->ctrl_idx, 1);

	usb_fill_bulk_urb(urb, dev->udev,
			usb_sndbulkpipe(dev->udev, PCAN_USBPRO_EP_CMDOUT),
			buf, PCAN_USB_MAX_CMD_LEN,
			pcan_usb_pro_restart_complete, dev);

	return usb_submit_urb(urb, GFP_ATOMIC);
}

static int pcan_usb_pro_drv_loaded(struct peak_usb_device *dev, int loaded)
{
	u8 *buffer;
	int err;

	buffer = kmalloc(PCAN_USBPRO_FCT_DRVLD_REQ_LEN, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer[0] = 0;
	buffer[1] = !!loaded;

	err = pcan_usb_pro_send_req(dev, PCAN_USBPRO_REQ_FCT,
				    PCAN_USBPRO_FCT_DRVLD, buffer,
				    PCAN_USBPRO_FCT_DRVLD_REQ_LEN);
	kfree(buffer);

	return err;
}

static inline
struct pcan_usb_pro_interface *pcan_usb_pro_dev_if(struct peak_usb_device *dev)
{
	struct pcan_usb_pro_device *pdev =
			container_of(dev, struct pcan_usb_pro_device, dev);
	return pdev->usb_if;
}

static int pcan_usb_pro_handle_canmsg(struct pcan_usb_pro_interface *usb_if,
				      struct pcan_usb_pro_rxmsg *rx)
{
	const unsigned int ctrl_idx = (rx->len >> 4) & 0x0f;
	struct peak_usb_device *dev = usb_if->dev[ctrl_idx];
	struct net_device *netdev = dev->netdev;
	struct can_frame *can_frame;
	struct sk_buff *skb;
	struct timeval tv;
	struct skb_shared_hwtstamps *hwts;

	skb = alloc_can_skb(netdev, &can_frame);
	if (!skb)
		return -ENOMEM;

	can_frame->can_id = le32_to_cpu(rx->id);
	can_frame->can_dlc = rx->len & 0x0f;

	if (rx->flags & PCAN_USBPRO_EXT)
		can_frame->can_id |= CAN_EFF_FLAG;

	if (rx->flags & PCAN_USBPRO_RTR)
		can_frame->can_id |= CAN_RTR_FLAG;
	else
		memcpy(can_frame->data, rx->data, can_frame->can_dlc);

	peak_usb_get_ts_tv(&usb_if->time_ref, le32_to_cpu(rx->ts32), &tv);
	hwts = skb_hwtstamps(skb);
	hwts->hwtstamp = timeval_to_ktime(tv);

	netif_rx(skb);
	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += can_frame->can_dlc;

	return 0;
}

static int pcan_usb_pro_handle_error(struct pcan_usb_pro_interface *usb_if,
				     struct pcan_usb_pro_rxstatus *er)
{
	const u16 raw_status = le16_to_cpu(er->status);
	const unsigned int ctrl_idx = (er->channel >> 4) & 0x0f;
	struct peak_usb_device *dev = usb_if->dev[ctrl_idx];
	struct net_device *netdev = dev->netdev;
	struct can_frame *can_frame;
	enum can_state new_state = CAN_STATE_ERROR_ACTIVE;
	u8 err_mask = 0;
	struct sk_buff *skb;
	struct timeval tv;
	struct skb_shared_hwtstamps *hwts;

	/* nothing should be sent while in BUS_OFF state */
	if (dev->can.state == CAN_STATE_BUS_OFF)
		return 0;

	if (!raw_status) {
		/* no error bit (back to active state) */
		dev->can.state = CAN_STATE_ERROR_ACTIVE;
		return 0;
	}

	if (raw_status & (PCAN_USBPRO_STATUS_OVERRUN |
			  PCAN_USBPRO_STATUS_QOVERRUN)) {
		/* trick to bypass next comparison and process other errors */
		new_state = CAN_STATE_MAX;
	}

	if (raw_status & PCAN_USBPRO_STATUS_BUS) {
		new_state = CAN_STATE_BUS_OFF;
	} else if (raw_status & PCAN_USBPRO_STATUS_ERROR) {
		u32 rx_err_cnt = (le32_to_cpu(er->err_frm) & 0x00ff0000) >> 16;
		u32 tx_err_cnt = (le32_to_cpu(er->err_frm) & 0xff000000) >> 24;

		if (rx_err_cnt > 127)
			err_mask |= CAN_ERR_CRTL_RX_PASSIVE;
		else if (rx_err_cnt > 96)
			err_mask |= CAN_ERR_CRTL_RX_WARNING;

		if (tx_err_cnt > 127)
			err_mask |= CAN_ERR_CRTL_TX_PASSIVE;
		else if (tx_err_cnt > 96)
			err_mask |= CAN_ERR_CRTL_TX_WARNING;

		if (err_mask & (CAN_ERR_CRTL_RX_WARNING |
				CAN_ERR_CRTL_TX_WARNING))
			new_state = CAN_STATE_ERROR_WARNING;
		else if (err_mask & (CAN_ERR_CRTL_RX_PASSIVE |
				     CAN_ERR_CRTL_TX_PASSIVE))
			new_state = CAN_STATE_ERROR_PASSIVE;
	}

	/* donot post any error if current state didn't change */
	if (dev->can.state == new_state)
		return 0;

	/* allocate an skb to store the error frame */
	skb = alloc_can_err_skb(netdev, &can_frame);
	if (!skb)
		return -ENOMEM;

	switch (new_state) {
	case CAN_STATE_BUS_OFF:
		can_frame->can_id |= CAN_ERR_BUSOFF;
		dev->can.can_stats.bus_off++;
		can_bus_off(netdev);
		break;

	case CAN_STATE_ERROR_PASSIVE:
		can_frame->can_id |= CAN_ERR_CRTL;
		can_frame->data[1] |= err_mask;
		dev->can.can_stats.error_passive++;
		break;

	case CAN_STATE_ERROR_WARNING:
		can_frame->can_id |= CAN_ERR_CRTL;
		can_frame->data[1] |= err_mask;
		dev->can.can_stats.error_warning++;
		break;

	case CAN_STATE_ERROR_ACTIVE:
		break;

	default:
		/* CAN_STATE_MAX (trick to handle other errors) */
		if (raw_status & PCAN_USBPRO_STATUS_OVERRUN) {
			can_frame->can_id |= CAN_ERR_PROT;
			can_frame->data[2] |= CAN_ERR_PROT_OVERLOAD;
			netdev->stats.rx_over_errors++;
			netdev->stats.rx_errors++;
		}

		if (raw_status & PCAN_USBPRO_STATUS_QOVERRUN) {
			can_frame->can_id |= CAN_ERR_CRTL;
			can_frame->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
			netdev->stats.rx_over_errors++;
			netdev->stats.rx_errors++;
		}

		new_state = CAN_STATE_ERROR_ACTIVE;
		break;
	}

	dev->can.state = new_state;

	peak_usb_get_ts_tv(&usb_if->time_ref, le32_to_cpu(er->ts32), &tv);
	hwts = skb_hwtstamps(skb);
	hwts->hwtstamp = timeval_to_ktime(tv);
	netif_rx(skb);
	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += can_frame->can_dlc;

	return 0;
}

static void pcan_usb_pro_handle_ts(struct pcan_usb_pro_interface *usb_if,
				   struct pcan_usb_pro_rxts *ts)
{
	/* should wait until clock is stabilized */
	if (usb_if->cm_ignore_count > 0)
		usb_if->cm_ignore_count--;
	else
		peak_usb_set_ts_now(&usb_if->time_ref,
				    le32_to_cpu(ts->ts64[1]));
}

/*
 * callback for bulk IN urb
 */
static int pcan_usb_pro_decode_buf(struct peak_usb_device *dev, struct urb *urb)
{
	struct pcan_usb_pro_interface *usb_if = pcan_usb_pro_dev_if(dev);
	struct net_device *netdev = dev->netdev;
	struct pcan_usb_pro_msg usb_msg;
	u8 *rec_ptr, *msg_end;
	u16 rec_cnt;
	int err = 0;

	rec_ptr = pcan_msg_init(&usb_msg, urb->transfer_buffer,
					urb->actual_length);
	if (!rec_ptr) {
		netdev_err(netdev, "bad msg hdr len %d\n", urb->actual_length);
		return -EINVAL;
	}

	/* loop reading all the records from the incoming message */
	msg_end = urb->transfer_buffer + urb->actual_length;
	rec_cnt = le16_to_cpu(*usb_msg.u.rec_cnt_rd);
	for (; rec_cnt > 0; rec_cnt--) {
		union pcan_usb_pro_rec *pr = (union pcan_usb_pro_rec *)rec_ptr;
		u16 sizeof_rec = pcan_usb_pro_sizeof_rec[pr->data_type];

		if (!sizeof_rec) {
			netdev_err(netdev,
				   "got unsupported rec in usb msg:\n");
			err = -ENOTSUPP;
			break;
		}

		/* check if the record goes out of current packet */
		if (rec_ptr + sizeof_rec > msg_end) {
			netdev_err(netdev,
				"got frag rec: should inc usb rx buf size\n");
			err = -EBADMSG;
			break;
		}

		switch (pr->data_type) {
		case PCAN_USBPRO_RXMSG8:
		case PCAN_USBPRO_RXMSG4:
		case PCAN_USBPRO_RXMSG0:
		case PCAN_USBPRO_RXRTR:
			err = pcan_usb_pro_handle_canmsg(usb_if, &pr->rx_msg);
			if (err < 0)
				goto fail;
			break;

		case PCAN_USBPRO_RXSTATUS:
			err = pcan_usb_pro_handle_error(usb_if, &pr->rx_status);
			if (err < 0)
				goto fail;
			break;

		case PCAN_USBPRO_RXTS:
			pcan_usb_pro_handle_ts(usb_if, &pr->rx_ts);
			break;

		default:
			netdev_err(netdev,
				   "unhandled rec type 0x%02x (%d): ignored\n",
				   pr->data_type, pr->data_type);
			break;
		}

		rec_ptr += sizeof_rec;
	}

fail:
	if (err)
		pcan_dump_mem("received msg",
			      urb->transfer_buffer, urb->actual_length);

	return err;
}

static int pcan_usb_pro_encode_msg(struct peak_usb_device *dev,
				   struct sk_buff *skb, u8 *obuf, size_t *size)
{
	struct can_frame *cf = (struct can_frame *)skb->data;
	u8 data_type, len, flags;
	struct pcan_usb_pro_msg usb_msg;

	pcan_msg_init_empty(&usb_msg, obuf, *size);

	if ((cf->can_id & CAN_RTR_FLAG) || (cf->can_dlc == 0))
		data_type = PCAN_USBPRO_TXMSG0;
	else if (cf->can_dlc <= 4)
		data_type = PCAN_USBPRO_TXMSG4;
	else
		data_type = PCAN_USBPRO_TXMSG8;

	len = (dev->ctrl_idx << 4) | (cf->can_dlc & 0x0f);

	flags = 0;
	if (cf->can_id & CAN_EFF_FLAG)
		flags |= 0x02;
	if (cf->can_id & CAN_RTR_FLAG)
		flags |= 0x01;

	pcan_msg_add_rec(&usb_msg, data_type, 0, flags, len, cf->can_id,
			 cf->data);

	*size = usb_msg.rec_buffer_len;

	return 0;
}

static int pcan_usb_pro_start(struct peak_usb_device *dev)
{
	struct pcan_usb_pro_device *pdev =
			container_of(dev, struct pcan_usb_pro_device, dev);
	int err;

	err = pcan_usb_pro_set_silent(dev,
				dev->can.ctrlmode & CAN_CTRLMODE_LISTENONLY);
	if (err)
		return err;

	/* filter mode: 0-> All OFF; 1->bypass */
	err = pcan_usb_pro_set_filter(dev, 1);
	if (err)
		return err;

	/* opening first device: */
	if (pdev->usb_if->dev_opened_count == 0) {
		/* reset time_ref */
		peak_usb_init_time_ref(&pdev->usb_if->time_ref, &pcan_usb_pro);

		/* ask device to send ts messages */
		err = pcan_usb_pro_set_ts(dev, 1);
	}

	pdev->usb_if->dev_opened_count++;

	return err;
}

/*
 * stop interface
 * (last chance before set bus off)
 */
static int pcan_usb_pro_stop(struct peak_usb_device *dev)
{
	struct pcan_usb_pro_device *pdev =
			container_of(dev, struct pcan_usb_pro_device, dev);

	/* turn off ts msgs for that interface if no other dev opened */
	if (pdev->usb_if->dev_opened_count == 1)
		pcan_usb_pro_set_ts(dev, 0);

	pdev->usb_if->dev_opened_count--;

	return 0;
}

/*
 * called when probing to initialize a device object.
 */
static int pcan_usb_pro_init(struct peak_usb_device *dev)
{
	struct pcan_usb_pro_device *pdev =
			container_of(dev, struct pcan_usb_pro_device, dev);
	struct pcan_usb_pro_interface *usb_if = NULL;
	struct pcan_usb_pro_fwinfo *fi = NULL;
	struct pcan_usb_pro_blinfo *bi = NULL;
	int err;

	/* do this for 1st channel only */
	if (!dev->prev_siblings) {
		/* allocate netdevices common structure attached to first one */
		usb_if = kzalloc(sizeof(struct pcan_usb_pro_interface),
				 GFP_KERNEL);
		fi = kmalloc(sizeof(struct pcan_usb_pro_fwinfo), GFP_KERNEL);
		bi = kmalloc(sizeof(struct pcan_usb_pro_blinfo), GFP_KERNEL);
		if (!usb_if || !fi || !bi) {
			err = -ENOMEM;
			goto err_out;
		}

		/* number of ts msgs to ignore before taking one into account */
		usb_if->cm_ignore_count = 5;

		/*
		 * explicit use of dev_xxx() instead of netdev_xxx() here:
		 * information displayed are related to the device itself, not
		 * to the canx netdevices.
		 */
		err = pcan_usb_pro_send_req(dev, PCAN_USBPRO_REQ_INFO,
					    PCAN_USBPRO_INFO_FW,
					    fi, sizeof(*fi));
		if (err) {
			dev_err(dev->netdev->dev.parent,
				"unable to read %s firmware info (err %d)\n",
				pcan_usb_pro.name, err);
			goto err_out;
		}

		err = pcan_usb_pro_send_req(dev, PCAN_USBPRO_REQ_INFO,
					    PCAN_USBPRO_INFO_BL,
					    bi, sizeof(*bi));
		if (err) {
			dev_err(dev->netdev->dev.parent,
				"unable to read %s bootloader info (err %d)\n",
				pcan_usb_pro.name, err);
			goto err_out;
		}

		/* tell the device the can driver is running */
		err = pcan_usb_pro_drv_loaded(dev, 1);
		if (err)
			goto err_out;

		dev_info(dev->netdev->dev.parent,
		     "PEAK-System %s hwrev %u serial %08X.%08X (%u channels)\n",
		     pcan_usb_pro.name,
		     bi->hw_rev, bi->serial_num_hi, bi->serial_num_lo,
		     pcan_usb_pro.ctrl_count);
	} else {
		usb_if = pcan_usb_pro_dev_if(dev->prev_siblings);
	}

	pdev->usb_if = usb_if;
	usb_if->dev[dev->ctrl_idx] = dev;

	/* set LED in default state (end of init phase) */
	pcan_usb_pro_set_led(dev, 0, 1);

	kfree(bi);
	kfree(fi);

	return 0;

 err_out:
	kfree(bi);
	kfree(fi);
	kfree(usb_if);

	return err;
}

static void pcan_usb_pro_exit(struct peak_usb_device *dev)
{
	struct pcan_usb_pro_device *pdev =
			container_of(dev, struct pcan_usb_pro_device, dev);

	/*
	 * when rmmod called before unplug and if down, should reset things
	 * before leaving
	 */
	if (dev->can.state != CAN_STATE_STOPPED) {
		/* set bus off on the corresponding channel */
		pcan_usb_pro_set_bus(dev, 0);
	}

	/* if channel #0 (only) */
	if (dev->ctrl_idx == 0) {
		/* turn off calibration message if any device were opened */
		if (pdev->usb_if->dev_opened_count > 0)
			pcan_usb_pro_set_ts(dev, 0);

		/* tell the PCAN-USB Pro device the driver is being unloaded */
		pcan_usb_pro_drv_loaded(dev, 0);
	}
}

/*
 * called when PCAN-USB Pro adapter is unplugged
 */
static void pcan_usb_pro_free(struct peak_usb_device *dev)
{
	/* last device: can free pcan_usb_pro_interface object now */
	if (!dev->prev_siblings && !dev->next_siblings)
		kfree(pcan_usb_pro_dev_if(dev));
}

/*
 * probe function for new PCAN-USB Pro usb interface
 */
int pcan_usb_pro_probe(struct usb_interface *intf)
{
	struct usb_host_interface *if_desc;
	int i;

	if_desc = intf->altsetting;

	/* check interface endpoint addresses */
	for (i = 0; i < if_desc->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep = &if_desc->endpoint[i].desc;

		/*
		 * below is the list of valid ep addreses. Any other ep address
		 * is considered as not-CAN interface address => no dev created
		 */
		switch (ep->bEndpointAddress) {
		case PCAN_USBPRO_EP_CMDOUT:
		case PCAN_USBPRO_EP_CMDIN:
		case PCAN_USBPRO_EP_MSGOUT_0:
		case PCAN_USBPRO_EP_MSGOUT_1:
		case PCAN_USBPRO_EP_MSGIN:
		case PCAN_USBPRO_EP_UNUSED:
			break;
		default:
			return -ENODEV;
		}
	}

	return 0;
}

/*
 * describe the PCAN-USB Pro adapter
 */
static const struct can_bittiming_const pcan_usb_pro_const = {
	.name = "pcan_usb_pro",
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

const struct peak_usb_adapter pcan_usb_pro = {
	.name = "PCAN-USB Pro",
	.device_id = PCAN_USBPRO_PRODUCT_ID,
	.ctrl_count = PCAN_USBPRO_CHANNEL_COUNT,
	.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES | CAN_CTRLMODE_LISTENONLY,
	.clock = {
		.freq = PCAN_USBPRO_CRYSTAL_HZ,
	},
	.bittiming_const = &pcan_usb_pro_const,

	/* size of device private data */
	.sizeof_dev_private = sizeof(struct pcan_usb_pro_device),

	/* timestamps usage */
	.ts_used_bits = 32,
	.ts_period = 1000000, /* calibration period in ts. */
	.us_per_ts_scale = 1, /* us = (ts * scale) >> shift */
	.us_per_ts_shift = 0,

	/* give here messages in/out endpoints */
	.ep_msg_in = PCAN_USBPRO_EP_MSGIN,
	.ep_msg_out = {PCAN_USBPRO_EP_MSGOUT_0, PCAN_USBPRO_EP_MSGOUT_1},

	/* size of rx/tx usb buffers */
	.rx_buffer_size = PCAN_USBPRO_RX_BUFFER_SIZE,
	.tx_buffer_size = PCAN_USBPRO_TX_BUFFER_SIZE,

	/* device callbacks */
	.intf_probe = pcan_usb_pro_probe,
	.dev_init = pcan_usb_pro_init,
	.dev_exit = pcan_usb_pro_exit,
	.dev_free = pcan_usb_pro_free,
	.dev_set_bus = pcan_usb_pro_set_bus,
	.dev_set_bittiming = pcan_usb_pro_set_bittiming,
	.dev_get_device_id = pcan_usb_pro_get_device_id,
	.dev_decode_buf = pcan_usb_pro_decode_buf,
	.dev_encode_msg = pcan_usb_pro_encode_msg,
	.dev_start = pcan_usb_pro_start,
	.dev_stop = pcan_usb_pro_stop,
	.dev_restart_async = pcan_usb_pro_restart_async,
};
