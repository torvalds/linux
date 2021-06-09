// SPDX-License-Identifier: GPL-2.0

/* Driver for ETAS GmbH ES58X USB CAN(-FD) Bus Interfaces.
 *
 * File es58x_core.c: Core logic to manage the network devices and the
 * USB interface.
 *
 * Copyright (c) 2019 Robert Bosch Engineering and Business Solutions. All rights reserved.
 * Copyright (c) 2020 ETAS K.K.. All rights reserved.
 * Copyright (c) 2020, 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/crc16.h>
#include <asm/unaligned.h>

#include "es58x_core.h"

#define DRV_VERSION "1.00"
MODULE_AUTHOR("Mailhol Vincent <mailhol.vincent@wanadoo.fr>");
MODULE_AUTHOR("Arunachalam Santhanam <arunachalam.santhanam@in.bosch.com>");
MODULE_DESCRIPTION("Socket CAN driver for ETAS ES58X USB adapters");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL v2");

#define ES58X_MODULE_NAME "etas_es58x"
#define ES58X_VENDOR_ID 0x108C
#define ES581_4_PRODUCT_ID 0x0159
#define ES582_1_PRODUCT_ID 0x0168
#define ES584_1_PRODUCT_ID 0x0169

/* ES58X FD has some interface protocols unsupported by this driver. */
#define ES58X_FD_INTERFACE_PROTOCOL 0

/* Table of devices which work with this driver. */
static const struct usb_device_id es58x_id_table[] = {
	{
		/* ETAS GmbH ES581.4 USB dual-channel CAN Bus Interface module. */
		USB_DEVICE(ES58X_VENDOR_ID, ES581_4_PRODUCT_ID),
		.driver_info = ES58X_DUAL_CHANNEL
	}, {
		/* ETAS GmbH ES582.1 USB dual-channel CAN FD Bus Interface module. */
		USB_DEVICE_INTERFACE_PROTOCOL(ES58X_VENDOR_ID, ES582_1_PRODUCT_ID,
					      ES58X_FD_INTERFACE_PROTOCOL),
		.driver_info = ES58X_DUAL_CHANNEL | ES58X_FD_FAMILY
	}, {
		/* ETAS GmbH ES584.1 USB single-channel CAN FD Bus Interface module. */
		USB_DEVICE_INTERFACE_PROTOCOL(ES58X_VENDOR_ID, ES584_1_PRODUCT_ID,
					      ES58X_FD_INTERFACE_PROTOCOL),
		.driver_info = ES58X_FD_FAMILY
	}, {
		/* Terminating entry */
	}
};

MODULE_DEVICE_TABLE(usb, es58x_id_table);

#define es58x_print_hex_dump(buf, len)					\
	print_hex_dump(KERN_DEBUG,					\
		       ES58X_MODULE_NAME " " __stringify(buf) ": ",	\
		       DUMP_PREFIX_NONE, 16, 1, buf, len, false)

#define es58x_print_hex_dump_debug(buf, len)				 \
	print_hex_dump_debug(ES58X_MODULE_NAME " " __stringify(buf) ": ",\
			     DUMP_PREFIX_NONE, 16, 1, buf, len, false)

/* The last two bytes of an ES58X command is a CRC16. The first two
 * bytes (the start of frame) are skipped and the CRC calculation
 * starts on the third byte.
 */
#define ES58X_CRC_CALC_OFFSET 2

/**
 * es58x_calculate_crc() - Compute the crc16 of a given URB.
 * @urb_cmd: The URB command for which we want to calculate the CRC.
 * @urb_len: Length of @urb_cmd. Must be at least bigger than 4
 *	(ES58X_CRC_CALC_OFFSET + sizeof(crc))
 *
 * Return: crc16 value.
 */
static u16 es58x_calculate_crc(const union es58x_urb_cmd *urb_cmd, u16 urb_len)
{
	u16 crc;
	ssize_t len = urb_len - ES58X_CRC_CALC_OFFSET - sizeof(crc);

	crc = crc16(0, &urb_cmd->raw_cmd[ES58X_CRC_CALC_OFFSET], len);
	return crc;
}

/**
 * es58x_get_crc() - Get the CRC value of a given URB.
 * @urb_cmd: The URB command for which we want to get the CRC.
 * @urb_len: Length of @urb_cmd. Must be at least bigger than 4
 *	(ES58X_CRC_CALC_OFFSET + sizeof(crc))
 *
 * Return: crc16 value.
 */
static u16 es58x_get_crc(const union es58x_urb_cmd *urb_cmd, u16 urb_len)
{
	u16 crc;
	const __le16 *crc_addr;

	crc_addr = (__le16 *)&urb_cmd->raw_cmd[urb_len - sizeof(crc)];
	crc = get_unaligned_le16(crc_addr);
	return crc;
}

/**
 * es58x_set_crc() - Set the CRC value of a given URB.
 * @urb_cmd: The URB command for which we want to get the CRC.
 * @urb_len: Length of @urb_cmd. Must be at least bigger than 4
 *	(ES58X_CRC_CALC_OFFSET + sizeof(crc))
 */
static void es58x_set_crc(union es58x_urb_cmd *urb_cmd, u16 urb_len)
{
	u16 crc;
	__le16 *crc_addr;

	crc = es58x_calculate_crc(urb_cmd, urb_len);
	crc_addr = (__le16 *)&urb_cmd->raw_cmd[urb_len - sizeof(crc)];
	put_unaligned_le16(crc, crc_addr);
}

/**
 * es58x_check_crc() - Validate the CRC value of a given URB.
 * @es58x_dev: ES58X device.
 * @urb_cmd: The URB command for which we want to check the CRC.
 * @urb_len: Length of @urb_cmd. Must be at least bigger than 4
 *	(ES58X_CRC_CALC_OFFSET + sizeof(crc))
 *
 * Return: zero on success, -EBADMSG if the CRC check fails.
 */
static int es58x_check_crc(struct es58x_device *es58x_dev,
			   const union es58x_urb_cmd *urb_cmd, u16 urb_len)
{
	u16 calculated_crc = es58x_calculate_crc(urb_cmd, urb_len);
	u16 expected_crc = es58x_get_crc(urb_cmd, urb_len);

	if (expected_crc != calculated_crc) {
		dev_err_ratelimited(es58x_dev->dev,
				    "%s: Bad CRC, urb_len: %d\n",
				    __func__, urb_len);
		return -EBADMSG;
	}

	return 0;
}

/**
 * es58x_timestamp_to_ns() - Convert a timestamp value received from a
 *	ES58X device to nanoseconds.
 * @timestamp: Timestamp received from a ES58X device.
 *
 * The timestamp received from ES58X is expressed in multiples of 0.5
 * micro seconds. This function converts it in to nanoseconds.
 *
 * Return: Timestamp value in nanoseconds.
 */
static u64 es58x_timestamp_to_ns(u64 timestamp)
{
	const u64 es58x_timestamp_ns_mult_coef = 500ULL;

	return es58x_timestamp_ns_mult_coef * timestamp;
}

/**
 * es58x_set_skb_timestamp() - Set the hardware timestamp of an skb.
 * @netdev: CAN network device.
 * @skb: socket buffer of a CAN message.
 * @timestamp: Timestamp received from an ES58X device.
 *
 * Used for both received and echo messages.
 */
static void es58x_set_skb_timestamp(struct net_device *netdev,
				    struct sk_buff *skb, u64 timestamp)
{
	struct es58x_device *es58x_dev = es58x_priv(netdev)->es58x_dev;
	struct skb_shared_hwtstamps *hwts;

	hwts = skb_hwtstamps(skb);
	/* Ignoring overflow (overflow on 64 bits timestamp with nano
	 * second precision would occur after more than 500 years).
	 */
	hwts->hwtstamp = ns_to_ktime(es58x_timestamp_to_ns(timestamp) +
				     es58x_dev->realtime_diff_ns);
}

/**
 * es58x_rx_timestamp() - Handle a received timestamp.
 * @es58x_dev: ES58X device.
 * @timestamp: Timestamp received from a ES58X device.
 *
 * Calculate the difference between the ES58X device and the kernel
 * internal clocks. This difference will be later used as an offset to
 * convert the timestamps of RX and echo messages to match the kernel
 * system time (e.g. convert to UNIX time).
 */
void es58x_rx_timestamp(struct es58x_device *es58x_dev, u64 timestamp)
{
	u64 ktime_real_ns = ktime_get_real_ns();
	u64 device_timestamp = es58x_timestamp_to_ns(timestamp);

	dev_dbg(es58x_dev->dev, "%s: request round-trip time: %llu ns\n",
		__func__, ktime_real_ns - es58x_dev->ktime_req_ns);

	es58x_dev->realtime_diff_ns =
	    (es58x_dev->ktime_req_ns + ktime_real_ns) / 2 - device_timestamp;
	es58x_dev->ktime_req_ns = 0;

	dev_dbg(es58x_dev->dev,
		"%s: Device timestamp: %llu, diff with kernel: %llu\n",
		__func__, device_timestamp, es58x_dev->realtime_diff_ns);
}

/**
 * es58x_set_realtime_diff_ns() - Calculate difference between the
 *	clocks of the ES58X device and the kernel
 * @es58x_dev: ES58X device.
 *
 * Request a timestamp from the ES58X device. Once the answer is
 * received, the timestamp difference will be set by the callback
 * function es58x_rx_timestamp().
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_set_realtime_diff_ns(struct es58x_device *es58x_dev)
{
	if (es58x_dev->ktime_req_ns) {
		dev_warn(es58x_dev->dev,
			 "%s: Previous request to set timestamp has not completed yet\n",
			 __func__);
		return -EBUSY;
	}

	es58x_dev->ktime_req_ns = ktime_get_real_ns();
	return es58x_dev->ops->get_timestamp(es58x_dev);
}

/**
 * es58x_is_can_state_active() - Is the network device in an active
 *	CAN state?
 * @netdev: CAN network device.
 *
 * The device is considered active if it is able to send or receive
 * CAN frames, that is to say if it is in any of
 * CAN_STATE_ERROR_ACTIVE, CAN_STATE_ERROR_WARNING or
 * CAN_STATE_ERROR_PASSIVE states.
 *
 * Caution: when recovering from a bus-off,
 * net/core/dev.c#can_restart() will call
 * net/core/dev.c#can_flush_echo_skb() without using any kind of
 * locks. For this reason, it is critical to guarantee that no TX or
 * echo operations (i.e. any access to priv->echo_skb[]) can be done
 * while this function is returning false.
 *
 * Return: true if the device is active, else returns false.
 */
static bool es58x_is_can_state_active(struct net_device *netdev)
{
	return es58x_priv(netdev)->can.state < CAN_STATE_BUS_OFF;
}

/**
 * es58x_is_echo_skb_threshold_reached() - Determine the limit of how
 *	many skb slots can be taken before we should stop the network
 *	queue.
 * @priv: ES58X private parameters related to the network device.
 *
 * We need to save enough free skb slots in order to be able to do
 * bulk send. This function can be used to determine when to wake or
 * stop the network queue in regard to the number of skb slots already
 * taken if the echo FIFO.
 *
 * Return: boolean.
 */
static bool es58x_is_echo_skb_threshold_reached(struct es58x_priv *priv)
{
	u32 num_echo_skb =  priv->tx_head - priv->tx_tail;
	u32 threshold = priv->can.echo_skb_max -
		priv->es58x_dev->param->tx_bulk_max + 1;

	return num_echo_skb >= threshold;
}

/**
 * es58x_can_free_echo_skb_tail() - Remove the oldest echo skb of the
 *	echo FIFO.
 * @netdev: CAN network device.
 *
 * Naming convention: the tail is the beginning of the FIFO, i.e. the
 * first skb to have entered the FIFO.
 */
static void es58x_can_free_echo_skb_tail(struct net_device *netdev)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	u16 fifo_mask = priv->es58x_dev->param->fifo_mask;
	unsigned int frame_len = 0;

	can_free_echo_skb(netdev, priv->tx_tail & fifo_mask, &frame_len);
	netdev_completed_queue(netdev, 1, frame_len);

	priv->tx_tail++;

	netdev->stats.tx_dropped++;
}

/**
 * es58x_can_get_echo_skb_recovery() - Try to re-sync the echo FIFO.
 * @netdev: CAN network device.
 * @rcv_packet_idx: Index
 *
 * This function should not be called under normal circumstances. In
 * the unlikely case that one or several URB packages get dropped by
 * the device, the index will get out of sync. Try to recover by
 * dropping the echo skb packets with older indexes.
 *
 * Return: zero if recovery was successful, -EINVAL otherwise.
 */
static int es58x_can_get_echo_skb_recovery(struct net_device *netdev,
					   u32 rcv_packet_idx)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	int ret = 0;

	netdev->stats.tx_errors++;

	if (net_ratelimit())
		netdev_warn(netdev,
			    "Bad echo packet index: %u. First index: %u, end index %u, num_echo_skb: %02u/%02u\n",
			    rcv_packet_idx, priv->tx_tail, priv->tx_head,
			    priv->tx_head - priv->tx_tail,
			    priv->can.echo_skb_max);

	if ((s32)(rcv_packet_idx - priv->tx_tail) < 0) {
		if (net_ratelimit())
			netdev_warn(netdev,
				    "Received echo index is from the past. Ignoring it\n");
		ret = -EINVAL;
	} else if ((s32)(rcv_packet_idx - priv->tx_head) >= 0) {
		if (net_ratelimit())
			netdev_err(netdev,
				   "Received echo index is from the future. Ignoring it\n");
		ret = -EINVAL;
	} else {
		if (net_ratelimit())
			netdev_warn(netdev,
				    "Recovery: dropping %u echo skb from index %u to %u\n",
				    rcv_packet_idx - priv->tx_tail,
				    priv->tx_tail, rcv_packet_idx - 1);
		while (priv->tx_tail != rcv_packet_idx) {
			if (priv->tx_tail == priv->tx_head)
				return -EINVAL;
			es58x_can_free_echo_skb_tail(netdev);
		}
	}
	return ret;
}

/**
 * es58x_can_get_echo_skb() - Get the skb from the echo FIFO and loop
 *	it back locally.
 * @netdev: CAN network device.
 * @rcv_packet_idx: Index of the first packet received from the device.
 * @tstamps: Array of hardware timestamps received from a ES58X device.
 * @pkts: Number of packets (and so, length of @tstamps).
 *
 * Callback function for when we receive a self reception
 * acknowledgment.  Retrieves the skb from the echo FIFO, sets its
 * hardware timestamp (the actual time it was sent) and loops it back
 * locally.
 *
 * The device has to be active (i.e. network interface UP and not in
 * bus off state or restarting).
 *
 * Packet indexes must be consecutive (i.e. index of first packet is
 * @rcv_packet_idx, index of second packet is @rcv_packet_idx + 1 and
 * index of last packet is @rcv_packet_idx + @pkts - 1).
 *
 * Return: zero on success.
 */
int es58x_can_get_echo_skb(struct net_device *netdev, u32 rcv_packet_idx,
			   u64 *tstamps, unsigned int pkts)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	unsigned int rx_total_frame_len = 0;
	unsigned int num_echo_skb = priv->tx_head - priv->tx_tail;
	int i;
	u16 fifo_mask = priv->es58x_dev->param->fifo_mask;

	if (!netif_running(netdev)) {
		if (net_ratelimit())
			netdev_info(netdev,
				    "%s: %s is down, dropping %d echo packets\n",
				    __func__, netdev->name, pkts);
		netdev->stats.tx_dropped += pkts;
		return 0;
	} else if (!es58x_is_can_state_active(netdev)) {
		if (net_ratelimit())
			netdev_dbg(netdev,
				   "Bus is off or device is restarting. Ignoring %u echo packets from index %u\n",
				   pkts, rcv_packet_idx);
		/* stats.tx_dropped will be (or was already)
		 * incremented by
		 * drivers/net/can/net/dev.c:can_flush_echo_skb().
		 */
		return 0;
	} else if (num_echo_skb == 0) {
		if (net_ratelimit())
			netdev_warn(netdev,
				    "Received %u echo packets from index: %u but echo skb queue is empty.\n",
				    pkts, rcv_packet_idx);
		netdev->stats.tx_dropped += pkts;
		return 0;
	}

	if (priv->tx_tail != rcv_packet_idx) {
		if (es58x_can_get_echo_skb_recovery(netdev, rcv_packet_idx) < 0) {
			if (net_ratelimit())
				netdev_warn(netdev,
					    "Could not find echo skb for echo packet index: %u\n",
					    rcv_packet_idx);
			return 0;
		}
	}
	if (num_echo_skb < pkts) {
		int pkts_drop = pkts - num_echo_skb;

		if (net_ratelimit())
			netdev_err(netdev,
				   "Received %u echo packets but have only %d echo skb. Dropping %d echo skb\n",
				   pkts, num_echo_skb, pkts_drop);
		netdev->stats.tx_dropped += pkts_drop;
		pkts -= pkts_drop;
	}

	for (i = 0; i < pkts; i++) {
		unsigned int skb_idx = priv->tx_tail & fifo_mask;
		struct sk_buff *skb = priv->can.echo_skb[skb_idx];
		unsigned int frame_len = 0;

		if (skb)
			es58x_set_skb_timestamp(netdev, skb, tstamps[i]);

		netdev->stats.tx_bytes += can_get_echo_skb(netdev, skb_idx,
							   &frame_len);
		rx_total_frame_len += frame_len;

		priv->tx_tail++;
	}

	netdev_completed_queue(netdev, pkts, rx_total_frame_len);
	netdev->stats.tx_packets += pkts;

	priv->err_passive_before_rtx_success = 0;
	if (!es58x_is_echo_skb_threshold_reached(priv))
		netif_wake_queue(netdev);

	return 0;
}

/**
 * es58x_can_reset_echo_fifo() - Reset the echo FIFO.
 * @netdev: CAN network device.
 *
 * The echo_skb array of struct can_priv will be flushed by
 * drivers/net/can/dev.c:can_flush_echo_skb(). This function resets
 * the parameters of the struct es58x_priv of our device and reset the
 * queue (c.f. BQL).
 */
static void es58x_can_reset_echo_fifo(struct net_device *netdev)
{
	struct es58x_priv *priv = es58x_priv(netdev);

	priv->tx_tail = 0;
	priv->tx_head = 0;
	priv->tx_urb = NULL;
	priv->err_passive_before_rtx_success = 0;
	netdev_reset_queue(netdev);
}

/**
 * es58x_flush_pending_tx_msg() - Reset the buffer for transmission messages.
 * @netdev: CAN network device.
 *
 * es58x_start_xmit() will queue up to tx_bulk_max messages in
 * &tx_urb buffer and do a bulk send of all messages in one single URB
 * (c.f. xmit_more flag). When the device recovers from a bus off
 * state or when the device stops, the tx_urb buffer might still have
 * pending messages in it and thus need to be flushed.
 */
static void es58x_flush_pending_tx_msg(struct net_device *netdev)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	struct es58x_device *es58x_dev = priv->es58x_dev;

	if (priv->tx_urb) {
		netdev_warn(netdev, "%s: dropping %d TX messages\n",
			    __func__, priv->tx_can_msg_cnt);
		netdev->stats.tx_dropped += priv->tx_can_msg_cnt;
		while (priv->tx_can_msg_cnt > 0) {
			unsigned int frame_len = 0;
			u16 fifo_mask = priv->es58x_dev->param->fifo_mask;

			priv->tx_head--;
			priv->tx_can_msg_cnt--;
			can_free_echo_skb(netdev, priv->tx_head & fifo_mask,
					  &frame_len);
			netdev_completed_queue(netdev, 1, frame_len);
		}
		usb_anchor_urb(priv->tx_urb, &priv->es58x_dev->tx_urbs_idle);
		atomic_inc(&es58x_dev->tx_urbs_idle_cnt);
		usb_free_urb(priv->tx_urb);
	}
	priv->tx_urb = NULL;
}

/**
 * es58x_tx_ack_msg() - Handle acknowledgment messages.
 * @netdev: CAN network device.
 * @tx_free_entries: Number of free entries in the device transmit FIFO.
 * @rx_cmd_ret_u32: error code as returned by the ES58X device.
 *
 * ES58X sends an acknowledgment message after a transmission request
 * is done. This is mandatory for the ES581.4 but is optional (and
 * deactivated in this driver) for the ES58X_FD family.
 *
 * Under normal circumstances, this function should never throw an
 * error message.
 *
 * Return: zero on success, errno when any error occurs.
 */
int es58x_tx_ack_msg(struct net_device *netdev, u16 tx_free_entries,
		     enum es58x_ret_u32 rx_cmd_ret_u32)
{
	struct es58x_priv *priv = es58x_priv(netdev);

	if (tx_free_entries <= priv->es58x_dev->param->tx_bulk_max) {
		if (net_ratelimit())
			netdev_err(netdev,
				   "Only %d entries left in device queue, num_echo_skb: %d/%d\n",
				   tx_free_entries,
				   priv->tx_head - priv->tx_tail,
				   priv->can.echo_skb_max);
		netif_stop_queue(netdev);
	}

	return es58x_rx_cmd_ret_u32(netdev, ES58X_RET_TYPE_TX_MSG,
				    rx_cmd_ret_u32);
}

/**
 * es58x_rx_can_msg() - Handle a received a CAN message.
 * @netdev: CAN network device.
 * @timestamp: Hardware time stamp (only relevant in rx branches).
 * @data: CAN payload.
 * @can_id: CAN ID.
 * @es58x_flags: Please refer to enum es58x_flag.
 * @dlc: Data Length Code (raw value).
 *
 * Fill up a CAN skb and post it.
 *
 * This function handles the case where the DLC of a classical CAN
 * frame is greater than CAN_MAX_DLEN (c.f. the len8_dlc field of
 * struct can_frame).
 *
 * Return: zero on success.
 */
int es58x_rx_can_msg(struct net_device *netdev, u64 timestamp, const u8 *data,
		     canid_t can_id, enum es58x_flag es58x_flags, u8 dlc)
{
	struct canfd_frame *cfd;
	struct can_frame *ccf;
	struct sk_buff *skb;
	u8 len;
	bool is_can_fd = !!(es58x_flags & ES58X_FLAG_FD_DATA);

	if (dlc > CAN_MAX_RAW_DLC) {
		netdev_err(netdev,
			   "%s: DLC is %d but maximum should be %d\n",
			   __func__, dlc, CAN_MAX_RAW_DLC);
		return -EMSGSIZE;
	}

	if (is_can_fd) {
		len = can_fd_dlc2len(dlc);
		skb = alloc_canfd_skb(netdev, &cfd);
	} else {
		len = can_cc_dlc2len(dlc);
		skb = alloc_can_skb(netdev, &ccf);
		cfd = (struct canfd_frame *)ccf;
	}
	if (!skb) {
		netdev->stats.rx_dropped++;
		return 0;
	}

	cfd->can_id = can_id;
	if (es58x_flags & ES58X_FLAG_EFF)
		cfd->can_id |= CAN_EFF_FLAG;
	if (is_can_fd) {
		cfd->len = len;
		if (es58x_flags & ES58X_FLAG_FD_BRS)
			cfd->flags |= CANFD_BRS;
		if (es58x_flags & ES58X_FLAG_FD_ESI)
			cfd->flags |= CANFD_ESI;
	} else {
		can_frame_set_cc_len(ccf, dlc, es58x_priv(netdev)->can.ctrlmode);
		if (es58x_flags & ES58X_FLAG_RTR) {
			ccf->can_id |= CAN_RTR_FLAG;
			len = 0;
		}
	}
	memcpy(cfd->data, data, len);
	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += len;

	es58x_set_skb_timestamp(netdev, skb, timestamp);
	netif_rx(skb);

	es58x_priv(netdev)->err_passive_before_rtx_success = 0;

	return 0;
}

/**
 * es58x_rx_err_msg() - Handle a received CAN event or error message.
 * @netdev: CAN network device.
 * @error: Error code.
 * @event: Event code.
 * @timestamp: Timestamp received from a ES58X device.
 *
 * Handle the errors and events received by the ES58X device, create
 * a CAN error skb and post it.
 *
 * In some rare cases the devices might get stuck alternating between
 * CAN_STATE_ERROR_PASSIVE and CAN_STATE_ERROR_WARNING. To prevent
 * this behavior, we force a bus off state if the device goes in
 * CAN_STATE_ERROR_WARNING for ES58X_MAX_CONSECUTIVE_WARN consecutive
 * times with no successful transmission or reception in between.
 *
 * Once the device is in bus off state, the only way to restart it is
 * through the drivers/net/can/dev.c:can_restart() function. The
 * device is technically capable to recover by itself under certain
 * circumstances, however, allowing self recovery would create
 * complex race conditions with drivers/net/can/dev.c:can_restart()
 * and thus was not implemented. To activate automatic restart, please
 * set the restart-ms parameter (e.g. ip link set can0 type can
 * restart-ms 100).
 *
 * If the bus is really instable, this function would try to send a
 * lot of log messages. Those are rate limited (i.e. you will see
 * messages such as "net_ratelimit: XXX callbacks suppressed" in
 * dmesg).
 *
 * Return: zero on success, errno when any error occurs.
 */
int es58x_rx_err_msg(struct net_device *netdev, enum es58x_err error,
		     enum es58x_event event, u64 timestamp)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	struct can_priv *can = netdev_priv(netdev);
	struct can_device_stats *can_stats = &can->can_stats;
	struct can_frame *cf = NULL;
	struct sk_buff *skb;
	int ret;

	if (!netif_running(netdev)) {
		if (net_ratelimit())
			netdev_info(netdev, "%s: %s is down, dropping packet\n",
				    __func__, netdev->name);
		netdev->stats.rx_dropped++;
		return 0;
	}

	if (error == ES58X_ERR_OK && event == ES58X_EVENT_OK) {
		netdev_err(netdev, "%s: Both error and event are zero\n",
			   __func__);
		return -EINVAL;
	}

	skb = alloc_can_err_skb(netdev, &cf);

	switch (error) {
	case ES58X_ERR_OK:	/* 0: No error */
		break;

	case ES58X_ERR_PROT_STUFF:
		if (net_ratelimit())
			netdev_dbg(netdev, "Error BITSTUFF\n");
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_STUFF;
		break;

	case ES58X_ERR_PROT_FORM:
		if (net_ratelimit())
			netdev_dbg(netdev, "Error FORMAT\n");
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_FORM;
		break;

	case ES58X_ERR_ACK:
		if (net_ratelimit())
			netdev_dbg(netdev, "Error ACK\n");
		if (cf)
			cf->can_id |= CAN_ERR_ACK;
		break;

	case ES58X_ERR_PROT_BIT:
		if (net_ratelimit())
			netdev_dbg(netdev, "Error BIT\n");
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_BIT;
		break;

	case ES58X_ERR_PROT_CRC:
		if (net_ratelimit())
			netdev_dbg(netdev, "Error CRC\n");
		if (cf)
			cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
		break;

	case ES58X_ERR_PROT_BIT1:
		if (net_ratelimit())
			netdev_dbg(netdev,
				   "Error: expected a recessive bit but monitored a dominant one\n");
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_BIT1;
		break;

	case ES58X_ERR_PROT_BIT0:
		if (net_ratelimit())
			netdev_dbg(netdev,
				   "Error expected a dominant bit but monitored a recessive one\n");
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_BIT0;
		break;

	case ES58X_ERR_PROT_OVERLOAD:
		if (net_ratelimit())
			netdev_dbg(netdev, "Error OVERLOAD\n");
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_OVERLOAD;
		break;

	case ES58X_ERR_PROT_UNSPEC:
		if (net_ratelimit())
			netdev_dbg(netdev, "Unspecified error\n");
		if (cf)
			cf->can_id |= CAN_ERR_PROT;
		break;

	default:
		if (net_ratelimit())
			netdev_err(netdev,
				   "%s: Unspecified error code 0x%04X\n",
				   __func__, (int)error);
		if (cf)
			cf->can_id |= CAN_ERR_PROT;
		break;
	}

	switch (event) {
	case ES58X_EVENT_OK:	/* 0: No event */
		break;

	case ES58X_EVENT_CRTL_ACTIVE:
		if (can->state == CAN_STATE_BUS_OFF) {
			netdev_err(netdev,
				   "%s: state transition: BUS OFF -> ACTIVE\n",
				   __func__);
		}
		if (net_ratelimit())
			netdev_dbg(netdev, "Event CAN BUS ACTIVE\n");
		if (cf)
			cf->data[1] |= CAN_ERR_CRTL_ACTIVE;
		can->state = CAN_STATE_ERROR_ACTIVE;
		break;

	case ES58X_EVENT_CRTL_PASSIVE:
		if (net_ratelimit())
			netdev_dbg(netdev, "Event CAN BUS PASSIVE\n");
		/* Either TX or RX error count reached passive state
		 * but we do not know which. Setting both flags by
		 * default.
		 */
		if (cf) {
			cf->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
			cf->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
		}
		if (can->state < CAN_STATE_BUS_OFF)
			can->state = CAN_STATE_ERROR_PASSIVE;
		can_stats->error_passive++;
		if (priv->err_passive_before_rtx_success < U8_MAX)
			priv->err_passive_before_rtx_success++;
		break;

	case ES58X_EVENT_CRTL_WARNING:
		if (net_ratelimit())
			netdev_dbg(netdev, "Event CAN BUS WARNING\n");
		/* Either TX or RX error count reached warning state
		 * but we do not know which. Setting both flags by
		 * default.
		 */
		if (cf) {
			cf->data[1] |= CAN_ERR_CRTL_RX_WARNING;
			cf->data[1] |= CAN_ERR_CRTL_TX_WARNING;
		}
		if (can->state < CAN_STATE_BUS_OFF)
			can->state = CAN_STATE_ERROR_WARNING;
		can_stats->error_warning++;
		break;

	case ES58X_EVENT_BUSOFF:
		if (net_ratelimit())
			netdev_dbg(netdev, "Event CAN BUS OFF\n");
		if (cf)
			cf->can_id |= CAN_ERR_BUSOFF;
		can_stats->bus_off++;
		netif_stop_queue(netdev);
		if (can->state != CAN_STATE_BUS_OFF) {
			can->state = CAN_STATE_BUS_OFF;
			can_bus_off(netdev);
			ret = can->do_set_mode(netdev, CAN_MODE_STOP);
			if (ret)
				return ret;
		}
		break;

	case ES58X_EVENT_SINGLE_WIRE:
		if (net_ratelimit())
			netdev_warn(netdev,
				    "Lost connection on either CAN high or CAN low\n");
		/* Lost connection on either CAN high or CAN
		 * low. Setting both flags by default.
		 */
		if (cf) {
			cf->data[4] |= CAN_ERR_TRX_CANH_NO_WIRE;
			cf->data[4] |= CAN_ERR_TRX_CANL_NO_WIRE;
		}
		break;

	default:
		if (net_ratelimit())
			netdev_err(netdev,
				   "%s: Unspecified event code 0x%04X\n",
				   __func__, (int)event);
		if (cf)
			cf->can_id |= CAN_ERR_CRTL;
		break;
	}

	/* driver/net/can/dev.c:can_restart() takes in account error
	 * messages in the RX stats. Doing the same here for
	 * consistency.
	 */
	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += CAN_ERR_DLC;

	if (cf) {
		if (cf->data[1])
			cf->can_id |= CAN_ERR_CRTL;
		if (cf->data[2] || cf->data[3]) {
			cf->can_id |= CAN_ERR_PROT;
			can_stats->bus_error++;
		}
		if (cf->data[4])
			cf->can_id |= CAN_ERR_TRX;

		es58x_set_skb_timestamp(netdev, skb, timestamp);
		netif_rx(skb);
	}

	if ((event & ES58X_EVENT_CRTL_PASSIVE) &&
	    priv->err_passive_before_rtx_success == ES58X_CONSECUTIVE_ERR_PASSIVE_MAX) {
		netdev_info(netdev,
			    "Got %d consecutive warning events with no successful RX or TX. Forcing bus-off\n",
			    priv->err_passive_before_rtx_success);
		return es58x_rx_err_msg(netdev, ES58X_ERR_OK,
					ES58X_EVENT_BUSOFF, timestamp);
	}

	return 0;
}

/**
 * es58x_cmd_ret_desc() - Convert a command type to a string.
 * @cmd_ret_type: Type of the command which triggered the return code.
 *
 * The final line (return "<unknown>") should not be reached. If this
 * is the case, there is an implementation bug.
 *
 * Return: a readable description of the @cmd_ret_type.
 */
static const char *es58x_cmd_ret_desc(enum es58x_ret_type cmd_ret_type)
{
	switch (cmd_ret_type) {
	case ES58X_RET_TYPE_SET_BITTIMING:
		return "Set bittiming";
	case ES58X_RET_TYPE_ENABLE_CHANNEL:
		return "Enable channel";
	case ES58X_RET_TYPE_DISABLE_CHANNEL:
		return "Disable channel";
	case ES58X_RET_TYPE_TX_MSG:
		return "Transmit message";
	case ES58X_RET_TYPE_RESET_RX:
		return "Reset RX";
	case ES58X_RET_TYPE_RESET_TX:
		return "Reset TX";
	case ES58X_RET_TYPE_DEVICE_ERR:
		return "Device error";
	}

	return "<unknown>";
};

/**
 * es58x_rx_cmd_ret_u8() - Handle the command's return code received
 *	from the ES58X device.
 * @dev: Device, only used for the dev_XXX() print functions.
 * @cmd_ret_type: Type of the command which triggered the return code.
 * @rx_cmd_ret_u8: Command error code as returned by the ES58X device.
 *
 * Handles the 8 bits command return code. Those are specific to the
 * ES581.4 device. The return value will eventually be used by
 * es58x_handle_urb_cmd() function which will take proper actions in
 * case of critical issues such and memory errors or bad CRC values.
 *
 * In contrast with es58x_rx_cmd_ret_u32(), the network device is
 * unknown.
 *
 * Return: zero on success, return errno when any error occurs.
 */
int es58x_rx_cmd_ret_u8(struct device *dev,
			enum es58x_ret_type cmd_ret_type,
			enum es58x_ret_u8 rx_cmd_ret_u8)
{
	const char *ret_desc = es58x_cmd_ret_desc(cmd_ret_type);

	switch (rx_cmd_ret_u8) {
	case ES58X_RET_U8_OK:
		dev_dbg_ratelimited(dev, "%s: OK\n", ret_desc);
		return 0;

	case ES58X_RET_U8_ERR_UNSPECIFIED_FAILURE:
		dev_err(dev, "%s: unspecified failure\n", ret_desc);
		return -EBADMSG;

	case ES58X_RET_U8_ERR_NO_MEM:
		dev_err(dev, "%s: device ran out of memory\n", ret_desc);
		return -ENOMEM;

	case ES58X_RET_U8_ERR_BAD_CRC:
		dev_err(dev, "%s: CRC of previous command is incorrect\n",
			ret_desc);
		return -EIO;

	default:
		dev_err(dev, "%s: returned unknown value: 0x%02X\n",
			ret_desc, rx_cmd_ret_u8);
		return -EBADMSG;
	}
}

/**
 * es58x_rx_cmd_ret_u32() - Handle the command return code received
 *	from the ES58X device.
 * @netdev: CAN network device.
 * @cmd_ret_type: Type of the command which triggered the return code.
 * @rx_cmd_ret_u32: error code as returned by the ES58X device.
 *
 * Handles the 32 bits command return code. The return value will
 * eventually be used by es58x_handle_urb_cmd() function which will
 * take proper actions in case of critical issues such and memory
 * errors or bad CRC values.
 *
 * Return: zero on success, errno when any error occurs.
 */
int es58x_rx_cmd_ret_u32(struct net_device *netdev,
			 enum es58x_ret_type cmd_ret_type,
			 enum es58x_ret_u32 rx_cmd_ret_u32)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	const struct es58x_operators *ops = priv->es58x_dev->ops;
	const char *ret_desc = es58x_cmd_ret_desc(cmd_ret_type);

	switch (rx_cmd_ret_u32) {
	case ES58X_RET_U32_OK:
		switch (cmd_ret_type) {
		case ES58X_RET_TYPE_ENABLE_CHANNEL:
			es58x_can_reset_echo_fifo(netdev);
			priv->can.state = CAN_STATE_ERROR_ACTIVE;
			netif_wake_queue(netdev);
			netdev_info(netdev,
				    "%s: %s (Serial Number %s): CAN%d channel becomes ready\n",
				    ret_desc, priv->es58x_dev->udev->product,
				    priv->es58x_dev->udev->serial,
				    priv->channel_idx + 1);
			break;

		case ES58X_RET_TYPE_TX_MSG:
			if (IS_ENABLED(CONFIG_VERBOSE_DEBUG) && net_ratelimit())
				netdev_vdbg(netdev, "%s: OK\n", ret_desc);
			break;

		default:
			netdev_dbg(netdev, "%s: OK\n", ret_desc);
			break;
		}
		return 0;

	case ES58X_RET_U32_ERR_UNSPECIFIED_FAILURE:
		if (cmd_ret_type == ES58X_RET_TYPE_ENABLE_CHANNEL) {
			int ret;

			netdev_warn(netdev,
				    "%s: channel is already opened, closing and re-opening it to reflect new configuration\n",
				    ret_desc);
			ret = ops->disable_channel(es58x_priv(netdev));
			if (ret)
				return ret;
			return ops->enable_channel(es58x_priv(netdev));
		}
		if (cmd_ret_type == ES58X_RET_TYPE_DISABLE_CHANNEL) {
			netdev_info(netdev,
				    "%s: channel is already closed\n", ret_desc);
			return 0;
		}
		netdev_err(netdev,
			   "%s: unspecified failure\n", ret_desc);
		return -EBADMSG;

	case ES58X_RET_U32_ERR_NO_MEM:
		netdev_err(netdev, "%s: device ran out of memory\n", ret_desc);
		return -ENOMEM;

	case ES58X_RET_U32_WARN_PARAM_ADJUSTED:
		netdev_warn(netdev,
			    "%s: some incompatible parameters have been adjusted\n",
			    ret_desc);
		return 0;

	case ES58X_RET_U32_WARN_TX_MAYBE_REORDER:
		netdev_warn(netdev,
			    "%s: TX messages might have been reordered\n",
			    ret_desc);
		return 0;

	case ES58X_RET_U32_ERR_TIMEDOUT:
		netdev_err(netdev, "%s: command timed out\n", ret_desc);
		return -ETIMEDOUT;

	case ES58X_RET_U32_ERR_FIFO_FULL:
		netdev_warn(netdev, "%s: fifo is full\n", ret_desc);
		return 0;

	case ES58X_RET_U32_ERR_BAD_CONFIG:
		netdev_err(netdev, "%s: bad configuration\n", ret_desc);
		return -EINVAL;

	case ES58X_RET_U32_ERR_NO_RESOURCE:
		netdev_err(netdev, "%s: no resource available\n", ret_desc);
		return -EBUSY;

	default:
		netdev_err(netdev, "%s returned unknown value: 0x%08X\n",
			   ret_desc, rx_cmd_ret_u32);
		return -EBADMSG;
	}
}

/**
 * es58x_increment_rx_errors() - Increment the network devices' error
 *	count.
 * @es58x_dev: ES58X device.
 *
 * If an error occurs on the early stages on receiving an URB command,
 * we might not be able to figure out on which network device the
 * error occurred. In such case, we arbitrarily increment the error
 * count of all the network devices attached to our ES58X device.
 */
static void es58x_increment_rx_errors(struct es58x_device *es58x_dev)
{
	int i;

	for (i = 0; i < es58x_dev->num_can_ch; i++)
		if (es58x_dev->netdev[i])
			es58x_dev->netdev[i]->stats.rx_errors++;
}

/**
 * es58x_handle_urb_cmd() - Handle the URB command
 * @es58x_dev: ES58X device.
 * @urb_cmd: The URB command received from the ES58X device, might not
 *	be aligned.
 *
 * Sends the URB command to the device specific function. Manages the
 * errors thrown back by those functions.
 */
static void es58x_handle_urb_cmd(struct es58x_device *es58x_dev,
				 const union es58x_urb_cmd *urb_cmd)
{
	const struct es58x_operators *ops = es58x_dev->ops;
	size_t cmd_len;
	int i, ret;

	ret = ops->handle_urb_cmd(es58x_dev, urb_cmd);
	switch (ret) {
	case 0:		/* OK */
		return;

	case -ENODEV:
		dev_err_ratelimited(es58x_dev->dev, "Device is not ready\n");
		break;

	case -EINVAL:
	case -EMSGSIZE:
	case -EBADRQC:
	case -EBADMSG:
	case -ECHRNG:
	case -ETIMEDOUT:
		cmd_len = es58x_get_urb_cmd_len(es58x_dev,
						ops->get_msg_len(urb_cmd));
		dev_err(es58x_dev->dev,
			"ops->handle_urb_cmd() returned error %pe",
			ERR_PTR(ret));
		es58x_print_hex_dump(urb_cmd, cmd_len);
		break;

	case -EFAULT:
	case -ENOMEM:
	case -EIO:
	default:
		dev_crit(es58x_dev->dev,
			 "ops->handle_urb_cmd() returned error %pe, detaching all network devices\n",
			 ERR_PTR(ret));
		for (i = 0; i < es58x_dev->num_can_ch; i++)
			if (es58x_dev->netdev[i])
				netif_device_detach(es58x_dev->netdev[i]);
		if (es58x_dev->ops->reset_device)
			es58x_dev->ops->reset_device(es58x_dev);
		break;
	}

	/* Because the urb command could not fully be parsed,
	 * channel_id is not confirmed. Incrementing rx_errors count
	 * of all channels.
	 */
	es58x_increment_rx_errors(es58x_dev);
}

/**
 * es58x_check_rx_urb() - Check the length and format of the URB command.
 * @es58x_dev: ES58X device.
 * @urb_cmd: The URB command received from the ES58X device, might not
 *	be aligned.
 * @urb_actual_len: The actual length of the URB command.
 *
 * Check if the first message of the received urb is valid, that is to
 * say that both the header and the length are coherent.
 *
 * Return:
 * the length of the first message of the URB on success.
 *
 * -ENODATA if the URB command is incomplete (in which case, the URB
 * command should be buffered and combined with the next URB to try to
 * reconstitute the URB command).
 *
 * -EOVERFLOW if the length is bigger than the maximum expected one.
 *
 * -EBADRQC if the start of frame does not match the expected value.
 */
static signed int es58x_check_rx_urb(struct es58x_device *es58x_dev,
				     const union es58x_urb_cmd *urb_cmd,
				     u32 urb_actual_len)
{
	const struct device *dev = es58x_dev->dev;
	const struct es58x_parameters *param = es58x_dev->param;
	u16 sof, msg_len;
	signed int urb_cmd_len, ret;

	if (urb_actual_len < param->urb_cmd_header_len) {
		dev_vdbg(dev,
			 "%s: Received %d bytes [%*ph]: header incomplete\n",
			 __func__, urb_actual_len, urb_actual_len,
			 urb_cmd->raw_cmd);
		return -ENODATA;
	}

	sof = get_unaligned_le16(&urb_cmd->sof);
	if (sof != param->rx_start_of_frame) {
		dev_err_ratelimited(es58x_dev->dev,
				    "%s: Expected sequence 0x%04X for start of frame but got 0x%04X.\n",
				    __func__, param->rx_start_of_frame, sof);
		return -EBADRQC;
	}

	msg_len = es58x_dev->ops->get_msg_len(urb_cmd);
	urb_cmd_len = es58x_get_urb_cmd_len(es58x_dev, msg_len);
	if (urb_cmd_len > param->rx_urb_cmd_max_len) {
		dev_err_ratelimited(es58x_dev->dev,
				    "%s: Biggest expected size for rx urb_cmd is %u but receive a command of size %d\n",
				    __func__,
				    param->rx_urb_cmd_max_len, urb_cmd_len);
		return -EOVERFLOW;
	} else if (urb_actual_len < urb_cmd_len) {
		dev_vdbg(dev, "%s: Received %02d/%02d bytes\n",
			 __func__, urb_actual_len, urb_cmd_len);
		return -ENODATA;
	}

	ret = es58x_check_crc(es58x_dev, urb_cmd, urb_cmd_len);
	if (ret)
		return ret;

	return urb_cmd_len;
}

/**
 * es58x_copy_to_cmd_buf() - Copy an array to the URB command buffer.
 * @es58x_dev: ES58X device.
 * @raw_cmd: the buffer we want to copy.
 * @raw_cmd_len: length of @raw_cmd.
 *
 * Concatenates @raw_cmd_len bytes of @raw_cmd to the end of the URB
 * command buffer.
 *
 * Return: zero on success, -EMSGSIZE if not enough space is available
 * to do the copy.
 */
static int es58x_copy_to_cmd_buf(struct es58x_device *es58x_dev,
				 u8 *raw_cmd, int raw_cmd_len)
{
	if (es58x_dev->rx_cmd_buf_len + raw_cmd_len >
	    es58x_dev->param->rx_urb_cmd_max_len)
		return -EMSGSIZE;

	memcpy(&es58x_dev->rx_cmd_buf.raw_cmd[es58x_dev->rx_cmd_buf_len],
	       raw_cmd, raw_cmd_len);
	es58x_dev->rx_cmd_buf_len += raw_cmd_len;

	return 0;
}

/**
 * es58x_split_urb_try_recovery() - Try to recover bad URB sequences.
 * @es58x_dev: ES58X device.
 * @raw_cmd: pointer to the buffer we want to copy.
 * @raw_cmd_len: length of @raw_cmd.
 *
 * Under some rare conditions, we might get incorrect URBs from the
 * device. From our observations, one of the valid URB gets replaced
 * by one from the past. The full root cause is not identified.
 *
 * This function looks for the next start of frame in the urb buffer
 * in order to try to recover.
 *
 * Such behavior was not observed on the devices of the ES58X FD
 * family and only seems to impact the ES581.4.
 *
 * Return: the number of bytes dropped on success, -EBADMSG if recovery failed.
 */
static int es58x_split_urb_try_recovery(struct es58x_device *es58x_dev,
					u8 *raw_cmd, size_t raw_cmd_len)
{
	union es58x_urb_cmd *urb_cmd;
	signed int urb_cmd_len;
	u16 sof;
	int dropped_bytes = 0;

	es58x_increment_rx_errors(es58x_dev);

	while (raw_cmd_len > sizeof(sof)) {
		urb_cmd = (union es58x_urb_cmd *)raw_cmd;
		sof = get_unaligned_le16(&urb_cmd->sof);

		if (sof == es58x_dev->param->rx_start_of_frame) {
			urb_cmd_len = es58x_check_rx_urb(es58x_dev,
							 urb_cmd, raw_cmd_len);
			if ((urb_cmd_len == -ENODATA) || urb_cmd_len > 0) {
				dev_info_ratelimited(es58x_dev->dev,
						     "Recovery successful! Dropped %d bytes (urb_cmd_len: %d)\n",
						     dropped_bytes,
						     urb_cmd_len);
				return dropped_bytes;
			}
		}
		raw_cmd++;
		raw_cmd_len--;
		dropped_bytes++;
	}

	dev_warn_ratelimited(es58x_dev->dev, "%s: Recovery failed\n", __func__);
	return -EBADMSG;
}

/**
 * es58x_handle_incomplete_cmd() - Reconstitute an URB command from
 *	different URB pieces.
 * @es58x_dev: ES58X device.
 * @urb: last urb buffer received.
 *
 * The device might split the URB commands in an arbitrary amount of
 * pieces. This function concatenates those in an URB buffer until a
 * full URB command is reconstituted and consume it.
 *
 * Return:
 * number of bytes consumed from @urb if successful.
 *
 * -ENODATA if the URB command is still incomplete.
 *
 * -EBADMSG if the URB command is incorrect.
 */
static signed int es58x_handle_incomplete_cmd(struct es58x_device *es58x_dev,
					      struct urb *urb)
{
	size_t cpy_len;
	signed int urb_cmd_len, tmp_cmd_buf_len, ret;

	tmp_cmd_buf_len = es58x_dev->rx_cmd_buf_len;
	cpy_len = min_t(int, es58x_dev->param->rx_urb_cmd_max_len -
			es58x_dev->rx_cmd_buf_len, urb->actual_length);
	ret = es58x_copy_to_cmd_buf(es58x_dev, urb->transfer_buffer, cpy_len);
	if (ret < 0)
		return ret;

	urb_cmd_len = es58x_check_rx_urb(es58x_dev, &es58x_dev->rx_cmd_buf,
					 es58x_dev->rx_cmd_buf_len);
	if (urb_cmd_len == -ENODATA) {
		return -ENODATA;
	} else if (urb_cmd_len < 0) {
		dev_err_ratelimited(es58x_dev->dev,
				    "Could not reconstitute incomplete command from previous URB, dropping %d bytes\n",
				    tmp_cmd_buf_len + urb->actual_length);
		dev_err_ratelimited(es58x_dev->dev,
				    "Error code: %pe, es58x_dev->rx_cmd_buf_len: %d, urb->actual_length: %u\n",
				    ERR_PTR(urb_cmd_len),
				    tmp_cmd_buf_len, urb->actual_length);
		es58x_print_hex_dump(&es58x_dev->rx_cmd_buf, tmp_cmd_buf_len);
		es58x_print_hex_dump(urb->transfer_buffer, urb->actual_length);
		return urb->actual_length;
	}

	es58x_handle_urb_cmd(es58x_dev, &es58x_dev->rx_cmd_buf);
	return urb_cmd_len - tmp_cmd_buf_len;	/* consumed length */
}

/**
 * es58x_split_urb() - Cut the received URB in individual URB commands.
 * @es58x_dev: ES58X device.
 * @urb: last urb buffer received.
 *
 * The device might send urb in bulk format (i.e. several URB commands
 * concatenated together). This function will split all the commands
 * contained in the urb.
 *
 * Return:
 * number of bytes consumed from @urb if successful.
 *
 * -ENODATA if the URB command is incomplete.
 *
 * -EBADMSG if the URB command is incorrect.
 */
static signed int es58x_split_urb(struct es58x_device *es58x_dev,
				  struct urb *urb)
{
	union es58x_urb_cmd *urb_cmd;
	u8 *raw_cmd = urb->transfer_buffer;
	s32 raw_cmd_len = urb->actual_length;
	int ret;

	if (es58x_dev->rx_cmd_buf_len != 0) {
		ret = es58x_handle_incomplete_cmd(es58x_dev, urb);
		if (ret != -ENODATA)
			es58x_dev->rx_cmd_buf_len = 0;
		if (ret < 0)
			return ret;

		raw_cmd += ret;
		raw_cmd_len -= ret;
	}

	while (raw_cmd_len > 0) {
		if (raw_cmd[0] == ES58X_HEARTBEAT) {
			raw_cmd++;
			raw_cmd_len--;
			continue;
		}
		urb_cmd = (union es58x_urb_cmd *)raw_cmd;
		ret = es58x_check_rx_urb(es58x_dev, urb_cmd, raw_cmd_len);
		if (ret > 0) {
			es58x_handle_urb_cmd(es58x_dev, urb_cmd);
		} else if (ret == -ENODATA) {
			es58x_copy_to_cmd_buf(es58x_dev, raw_cmd, raw_cmd_len);
			return -ENODATA;
		} else if (ret < 0) {
			ret = es58x_split_urb_try_recovery(es58x_dev, raw_cmd,
							   raw_cmd_len);
			if (ret < 0)
				return ret;
		}
		raw_cmd += ret;
		raw_cmd_len -= ret;
	}

	return 0;
}

/**
 * es58x_read_bulk_callback() - Callback for reading data from device.
 * @urb: last urb buffer received.
 *
 * This function gets eventually called each time an URB is received
 * from the ES58X device.
 *
 * Checks urb status, calls read function and resubmits urb read
 * operation.
 */
static void es58x_read_bulk_callback(struct urb *urb)
{
	struct es58x_device *es58x_dev = urb->context;
	const struct device *dev = es58x_dev->dev;
	int i, ret;

	switch (urb->status) {
	case 0:		/* success */
		break;

	case -EOVERFLOW:
		dev_err_ratelimited(dev, "%s: error %pe\n",
				    __func__, ERR_PTR(urb->status));
		es58x_print_hex_dump_debug(urb->transfer_buffer,
					   urb->transfer_buffer_length);
		goto resubmit_urb;

	case -EPROTO:
		dev_warn_ratelimited(dev, "%s: error %pe. Device unplugged?\n",
				     __func__, ERR_PTR(urb->status));
		goto free_urb;

	case -ENOENT:
	case -EPIPE:
		dev_err_ratelimited(dev, "%s: error %pe\n",
				    __func__, ERR_PTR(urb->status));
		goto free_urb;

	case -ESHUTDOWN:
		dev_dbg_ratelimited(dev, "%s: error %pe\n",
				    __func__, ERR_PTR(urb->status));
		goto free_urb;

	default:
		dev_err_ratelimited(dev, "%s: error %pe\n",
				    __func__, ERR_PTR(urb->status));
		goto resubmit_urb;
	}

	ret = es58x_split_urb(es58x_dev, urb);
	if ((ret != -ENODATA) && ret < 0) {
		dev_err(es58x_dev->dev, "es58x_split_urb() returned error %pe",
			ERR_PTR(ret));
		es58x_print_hex_dump_debug(urb->transfer_buffer,
					   urb->actual_length);

		/* Because the urb command could not be parsed,
		 * channel_id is not confirmed. Incrementing rx_errors
		 * count of all channels.
		 */
		es58x_increment_rx_errors(es58x_dev);
	}

 resubmit_urb:
	usb_fill_bulk_urb(urb, es58x_dev->udev, es58x_dev->rx_pipe,
			  urb->transfer_buffer, urb->transfer_buffer_length,
			  es58x_read_bulk_callback, es58x_dev);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret == -ENODEV) {
		for (i = 0; i < es58x_dev->num_can_ch; i++)
			if (es58x_dev->netdev[i])
				netif_device_detach(es58x_dev->netdev[i]);
	} else if (ret)
		dev_err_ratelimited(dev,
				    "Failed resubmitting read bulk urb: %pe\n",
				    ERR_PTR(ret));
	return;

 free_urb:
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
}

/**
 * es58x_write_bulk_callback() - Callback after writing data to the device.
 * @urb: urb buffer which was previously submitted.
 *
 * This function gets eventually called each time an URB was sent to
 * the ES58X device.
 *
 * Puts the @urb back to the urbs idle anchor and tries to restart the
 * network queue.
 */
static void es58x_write_bulk_callback(struct urb *urb)
{
	struct net_device *netdev = urb->context;
	struct es58x_device *es58x_dev = es58x_priv(netdev)->es58x_dev;

	switch (urb->status) {
	case 0:		/* success */
		break;

	case -EOVERFLOW:
		if (net_ratelimit())
			netdev_err(netdev, "%s: error %pe\n",
				   __func__, ERR_PTR(urb->status));
		es58x_print_hex_dump(urb->transfer_buffer,
				     urb->transfer_buffer_length);
		break;

	case -ENOENT:
		if (net_ratelimit())
			netdev_dbg(netdev, "%s: error %pe\n",
				   __func__, ERR_PTR(urb->status));
		usb_free_coherent(urb->dev,
				  es58x_dev->param->tx_urb_cmd_max_len,
				  urb->transfer_buffer, urb->transfer_dma);
		return;

	default:
		if (net_ratelimit())
			netdev_info(netdev, "%s: error %pe\n",
				    __func__, ERR_PTR(urb->status));
		break;
	}

	usb_anchor_urb(urb, &es58x_dev->tx_urbs_idle);
	atomic_inc(&es58x_dev->tx_urbs_idle_cnt);
}

/**
 * es58x_alloc_urb() - Allocate memory for an URB and its transfer
 *	buffer.
 * @es58x_dev: ES58X device.
 * @urb: URB to be allocated.
 * @buf: used to return DMA address of buffer.
 * @buf_len: requested buffer size.
 * @mem_flags: affect whether allocation may block.
 *
 * Allocates an URB and its @transfer_buffer and set its @transfer_dma
 * address.
 *
 * This function is used at start-up to allocate all RX URBs at once
 * and during run time for TX URBs.
 *
 * Return: zero on success, -ENOMEM if no memory is available.
 */
static int es58x_alloc_urb(struct es58x_device *es58x_dev, struct urb **urb,
			   u8 **buf, size_t buf_len, gfp_t mem_flags)
{
	*urb = usb_alloc_urb(0, mem_flags);
	if (!*urb) {
		dev_err(es58x_dev->dev, "No memory left for URBs\n");
		return -ENOMEM;
	}

	*buf = usb_alloc_coherent(es58x_dev->udev, buf_len,
				  mem_flags, &(*urb)->transfer_dma);
	if (!*buf) {
		dev_err(es58x_dev->dev, "No memory left for USB buffer\n");
		usb_free_urb(*urb);
		return -ENOMEM;
	}

	(*urb)->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return 0;
}

/**
 * es58x_get_tx_urb() - Get an URB for transmission.
 * @es58x_dev: ES58X device.
 *
 * Gets an URB from the idle urbs anchor or allocate a new one if the
 * anchor is empty.
 *
 * If there are more than ES58X_TX_URBS_MAX in the idle anchor, do
 * some garbage collection. The garbage collection is done here
 * instead of within es58x_write_bulk_callback() because
 * usb_free_coherent() should not be used in IRQ context:
 * c.f. WARN_ON(irqs_disabled()) in dma_free_attrs().
 *
 * Return: a pointer to an URB on success, NULL if no memory is
 * available.
 */
static struct urb *es58x_get_tx_urb(struct es58x_device *es58x_dev)
{
	atomic_t *idle_cnt = &es58x_dev->tx_urbs_idle_cnt;
	struct urb *urb = usb_get_from_anchor(&es58x_dev->tx_urbs_idle);

	if (!urb) {
		size_t tx_buf_len;
		u8 *buf;

		tx_buf_len = es58x_dev->param->tx_urb_cmd_max_len;
		if (es58x_alloc_urb(es58x_dev, &urb, &buf, tx_buf_len,
				    GFP_ATOMIC))
			return NULL;

		usb_fill_bulk_urb(urb, es58x_dev->udev, es58x_dev->tx_pipe,
				  buf, tx_buf_len, NULL, NULL);
		return urb;
	}

	while (atomic_dec_return(idle_cnt) > ES58X_TX_URBS_MAX) {
		/* Garbage collector */
		struct urb *tmp = usb_get_from_anchor(&es58x_dev->tx_urbs_idle);

		if (!tmp)
			break;
		usb_free_coherent(tmp->dev,
				  es58x_dev->param->tx_urb_cmd_max_len,
				  tmp->transfer_buffer, tmp->transfer_dma);
		usb_free_urb(tmp);
	}

	return urb;
}

/**
 * es58x_submit_urb() - Send data to the device.
 * @es58x_dev: ES58X device.
 * @urb: URB to be sent.
 * @netdev: CAN network device.
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_submit_urb(struct es58x_device *es58x_dev, struct urb *urb,
			    struct net_device *netdev)
{
	int ret;

	es58x_set_crc(urb->transfer_buffer, urb->transfer_buffer_length);
	usb_fill_bulk_urb(urb, es58x_dev->udev, es58x_dev->tx_pipe,
			  urb->transfer_buffer, urb->transfer_buffer_length,
			  es58x_write_bulk_callback, netdev);
	usb_anchor_urb(urb, &es58x_dev->tx_urbs_busy);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		netdev_err(netdev, "%s: USB send urb failure: %pe\n",
			   __func__, ERR_PTR(ret));
		usb_unanchor_urb(urb);
		usb_free_coherent(urb->dev,
				  es58x_dev->param->tx_urb_cmd_max_len,
				  urb->transfer_buffer, urb->transfer_dma);
	}
	usb_free_urb(urb);

	return ret;
}

/**
 * es58x_send_msg() - Prepare an URB and submit it.
 * @es58x_dev: ES58X device.
 * @cmd_type: Command type.
 * @cmd_id: Command ID.
 * @msg: ES58X message to be sent.
 * @msg_len: Length of @msg.
 * @channel_idx: Index of the network device.
 *
 * Creates an URB command from a given message, sets the header and the
 * CRC and then submits it.
 *
 * Return: zero on success, errno when any error occurs.
 */
int es58x_send_msg(struct es58x_device *es58x_dev, u8 cmd_type, u8 cmd_id,
		   const void *msg, u16 msg_len, int channel_idx)
{
	struct net_device *netdev;
	union es58x_urb_cmd *urb_cmd;
	struct urb *urb;
	int urb_cmd_len;

	if (channel_idx == ES58X_CHANNEL_IDX_NA)
		netdev = es58x_dev->netdev[0];	/* Default to first channel */
	else
		netdev = es58x_dev->netdev[channel_idx];

	urb_cmd_len = es58x_get_urb_cmd_len(es58x_dev, msg_len);
	if (urb_cmd_len > es58x_dev->param->tx_urb_cmd_max_len)
		return -EOVERFLOW;

	urb = es58x_get_tx_urb(es58x_dev);
	if (!urb)
		return -ENOMEM;

	urb_cmd = urb->transfer_buffer;
	es58x_dev->ops->fill_urb_header(urb_cmd, cmd_type, cmd_id,
					channel_idx, msg_len);
	memcpy(&urb_cmd->raw_cmd[es58x_dev->param->urb_cmd_header_len],
	       msg, msg_len);
	urb->transfer_buffer_length = urb_cmd_len;

	return es58x_submit_urb(es58x_dev, urb, netdev);
}

/**
 * es58x_alloc_rx_urbs() - Allocate RX URBs.
 * @es58x_dev: ES58X device.
 *
 * Allocate URBs for reception and anchor them.
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_alloc_rx_urbs(struct es58x_device *es58x_dev)
{
	const struct device *dev = es58x_dev->dev;
	const struct es58x_parameters *param = es58x_dev->param;
	size_t rx_buf_len = es58x_dev->rx_max_packet_size;
	struct urb *urb;
	u8 *buf;
	int i;
	int ret = -EINVAL;

	for (i = 0; i < param->rx_urb_max; i++) {
		ret = es58x_alloc_urb(es58x_dev, &urb, &buf, rx_buf_len,
				      GFP_KERNEL);
		if (ret)
			break;

		usb_fill_bulk_urb(urb, es58x_dev->udev, es58x_dev->rx_pipe,
				  buf, rx_buf_len, es58x_read_bulk_callback,
				  es58x_dev);
		usb_anchor_urb(urb, &es58x_dev->rx_urbs);

		ret = usb_submit_urb(urb, GFP_KERNEL);
		if (ret) {
			usb_unanchor_urb(urb);
			usb_free_coherent(es58x_dev->udev, rx_buf_len,
					  buf, urb->transfer_dma);
			usb_free_urb(urb);
			break;
		}
		usb_free_urb(urb);
	}

	if (i == 0) {
		dev_err(dev, "%s: Could not setup any rx URBs\n", __func__);
		return ret;
	}
	dev_dbg(dev, "%s: Allocated %d rx URBs each of size %zu\n",
		__func__, i, rx_buf_len);

	return ret;
}

/**
 * es58x_free_urbs() - Free all the TX and RX URBs.
 * @es58x_dev: ES58X device.
 */
static void es58x_free_urbs(struct es58x_device *es58x_dev)
{
	struct urb *urb;

	if (!usb_wait_anchor_empty_timeout(&es58x_dev->tx_urbs_busy, 1000)) {
		dev_err(es58x_dev->dev, "%s: Timeout, some TX urbs still remain\n",
			__func__);
		usb_kill_anchored_urbs(&es58x_dev->tx_urbs_busy);
	}

	while ((urb = usb_get_from_anchor(&es58x_dev->tx_urbs_idle)) != NULL) {
		usb_free_coherent(urb->dev, es58x_dev->param->tx_urb_cmd_max_len,
				  urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		atomic_dec(&es58x_dev->tx_urbs_idle_cnt);
	}
	if (atomic_read(&es58x_dev->tx_urbs_idle_cnt))
		dev_err(es58x_dev->dev,
			"All idle urbs were freed but tx_urb_idle_cnt is %d\n",
			atomic_read(&es58x_dev->tx_urbs_idle_cnt));

	usb_kill_anchored_urbs(&es58x_dev->rx_urbs);
}

/**
 * es58x_open() - Enable the network device.
 * @netdev: CAN network device.
 *
 * Called when the network transitions to the up state. Allocate the
 * URB resources if needed and open the channel.
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_open(struct net_device *netdev)
{
	struct es58x_device *es58x_dev = es58x_priv(netdev)->es58x_dev;
	int ret;

	if (atomic_inc_return(&es58x_dev->opened_channel_cnt) == 1) {
		ret = es58x_alloc_rx_urbs(es58x_dev);
		if (ret)
			return ret;

		ret = es58x_set_realtime_diff_ns(es58x_dev);
		if (ret)
			goto free_urbs;
	}

	ret = open_candev(netdev);
	if (ret)
		goto free_urbs;

	ret = es58x_dev->ops->enable_channel(es58x_priv(netdev));
	if (ret)
		goto free_urbs;

	netif_start_queue(netdev);

	return ret;

 free_urbs:
	if (atomic_dec_and_test(&es58x_dev->opened_channel_cnt))
		es58x_free_urbs(es58x_dev);
	netdev_err(netdev, "%s: Could not open the network device: %pe\n",
		   __func__, ERR_PTR(ret));

	return ret;
}

/**
 * es58x_stop() - Disable the network device.
 * @netdev: CAN network device.
 *
 * Called when the network transitions to the down state. If all the
 * channels of the device are closed, free the URB resources which are
 * not needed anymore.
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_stop(struct net_device *netdev)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	struct es58x_device *es58x_dev = priv->es58x_dev;
	int ret;

	netif_stop_queue(netdev);
	ret = es58x_dev->ops->disable_channel(priv);
	if (ret)
		return ret;

	priv->can.state = CAN_STATE_STOPPED;
	es58x_can_reset_echo_fifo(netdev);
	close_candev(netdev);

	es58x_flush_pending_tx_msg(netdev);

	if (atomic_dec_and_test(&es58x_dev->opened_channel_cnt))
		es58x_free_urbs(es58x_dev);

	return 0;
}

/**
 * es58x_xmit_commit() - Send the bulk urb.
 * @netdev: CAN network device.
 *
 * Do the bulk send. This function should be called only once by bulk
 * transmission.
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_xmit_commit(struct net_device *netdev)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	int ret;

	if (!es58x_is_can_state_active(netdev))
		return -ENETDOWN;

	if (es58x_is_echo_skb_threshold_reached(priv))
		netif_stop_queue(netdev);

	ret = es58x_submit_urb(priv->es58x_dev, priv->tx_urb, netdev);
	if (ret == 0)
		priv->tx_urb = NULL;

	return ret;
}

/**
 * es58x_xmit_more() - Can we put more packets?
 * @priv: ES58X private parameters related to the network device.
 *
 * Return: true if we can put more, false if it is time to send.
 */
static bool es58x_xmit_more(struct es58x_priv *priv)
{
	unsigned int free_slots =
	    priv->can.echo_skb_max - (priv->tx_head - priv->tx_tail);

	return netdev_xmit_more() && free_slots > 0 &&
		priv->tx_can_msg_cnt < priv->es58x_dev->param->tx_bulk_max;
}

/**
 * es58x_start_xmit() - Transmit an skb.
 * @skb: socket buffer of a CAN message.
 * @netdev: CAN network device.
 *
 * Called when a packet needs to be transmitted.
 *
 * This function relies on Byte Queue Limits (BQL). The main benefit
 * is to increase the throughput by allowing bulk transfers
 * (c.f. xmit_more flag).
 *
 * Queues up to tx_bulk_max messages in &tx_urb buffer and does
 * a bulk send of all messages in one single URB.
 *
 * Return: NETDEV_TX_OK regardless of if we could transmit the @skb or
 *	had to drop it.
 */
static netdev_tx_t es58x_start_xmit(struct sk_buff *skb,
				    struct net_device *netdev)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	struct es58x_device *es58x_dev = priv->es58x_dev;
	unsigned int frame_len;
	int ret;

	if (can_dropped_invalid_skb(netdev, skb)) {
		if (priv->tx_urb)
			goto xmit_commit;
		return NETDEV_TX_OK;
	}

	if (priv->tx_urb && priv->tx_can_msg_is_fd != can_is_canfd_skb(skb)) {
		/* Can not do bulk send with mixed CAN and CAN FD frames. */
		ret = es58x_xmit_commit(netdev);
		if (ret)
			goto drop_skb;
	}

	if (!priv->tx_urb) {
		priv->tx_urb = es58x_get_tx_urb(es58x_dev);
		if (!priv->tx_urb) {
			ret = -ENOMEM;
			goto drop_skb;
		}
		priv->tx_can_msg_cnt = 0;
		priv->tx_can_msg_is_fd = can_is_canfd_skb(skb);
	}

	ret = es58x_dev->ops->tx_can_msg(priv, skb);
	if (ret)
		goto drop_skb;

	frame_len = can_skb_get_frame_len(skb);
	ret = can_put_echo_skb(skb, netdev,
			       priv->tx_head & es58x_dev->param->fifo_mask,
			       frame_len);
	if (ret)
		goto xmit_failure;
	netdev_sent_queue(netdev, frame_len);

	priv->tx_head++;
	priv->tx_can_msg_cnt++;

 xmit_commit:
	if (!es58x_xmit_more(priv)) {
		ret = es58x_xmit_commit(netdev);
		if (ret)
			goto xmit_failure;
	}

	return NETDEV_TX_OK;

 drop_skb:
	dev_kfree_skb(skb);
	netdev->stats.tx_dropped++;
 xmit_failure:
	netdev_warn(netdev, "%s: send message failure: %pe\n",
		    __func__, ERR_PTR(ret));
	netdev->stats.tx_errors++;
	es58x_flush_pending_tx_msg(netdev);
	return NETDEV_TX_OK;
}

static const struct net_device_ops es58x_netdev_ops = {
	.ndo_open = es58x_open,
	.ndo_stop = es58x_stop,
	.ndo_start_xmit = es58x_start_xmit
};

/**
 * es58x_set_mode() - Change network device mode.
 * @netdev: CAN network device.
 * @mode: either %CAN_MODE_START, %CAN_MODE_STOP or %CAN_MODE_SLEEP
 *
 * Currently, this function is only used to stop and restart the
 * channel during a bus off event (c.f. es58x_rx_err_msg() and
 * drivers/net/can/dev.c:can_restart() which are the two only
 * callers).
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_set_mode(struct net_device *netdev, enum can_mode mode)
{
	struct es58x_priv *priv = es58x_priv(netdev);

	switch (mode) {
	case CAN_MODE_START:
		switch (priv->can.state) {
		case CAN_STATE_BUS_OFF:
			return priv->es58x_dev->ops->enable_channel(priv);

		case CAN_STATE_STOPPED:
			return es58x_open(netdev);

		case CAN_STATE_ERROR_ACTIVE:
		case CAN_STATE_ERROR_WARNING:
		case CAN_STATE_ERROR_PASSIVE:
		default:
			return 0;
		}

	case CAN_MODE_STOP:
		switch (priv->can.state) {
		case CAN_STATE_STOPPED:
			return 0;

		case CAN_STATE_ERROR_ACTIVE:
		case CAN_STATE_ERROR_WARNING:
		case CAN_STATE_ERROR_PASSIVE:
		case CAN_STATE_BUS_OFF:
		default:
			return priv->es58x_dev->ops->disable_channel(priv);
		}

	case CAN_MODE_SLEEP:
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * es58x_init_priv() - Initialize private parameters.
 * @es58x_dev: ES58X device.
 * @priv: ES58X private parameters related to the network device.
 * @channel_idx: Index of the network device.
 */
static void es58x_init_priv(struct es58x_device *es58x_dev,
			    struct es58x_priv *priv, int channel_idx)
{
	const struct es58x_parameters *param = es58x_dev->param;
	struct can_priv *can = &priv->can;

	priv->es58x_dev = es58x_dev;
	priv->channel_idx = channel_idx;
	priv->tx_urb = NULL;
	priv->tx_can_msg_cnt = 0;

	can->bittiming_const = param->bittiming_const;
	if (param->ctrlmode_supported & CAN_CTRLMODE_FD) {
		can->data_bittiming_const = param->data_bittiming_const;
		can->tdc_const = param->tdc_const;
	}
	can->bitrate_max = param->bitrate_max;
	can->clock = param->clock;
	can->state = CAN_STATE_STOPPED;
	can->ctrlmode_supported = param->ctrlmode_supported;
	can->do_set_mode = es58x_set_mode;
}

/**
 * es58x_init_netdev() - Initialize the network device.
 * @es58x_dev: ES58X device.
 * @channel_idx: Index of the network device.
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_init_netdev(struct es58x_device *es58x_dev, int channel_idx)
{
	struct net_device *netdev;
	struct device *dev = es58x_dev->dev;
	int ret;

	netdev = alloc_candev(sizeof(struct es58x_priv),
			      es58x_dev->param->fifo_mask + 1);
	if (!netdev) {
		dev_err(dev, "Could not allocate candev\n");
		return -ENOMEM;
	}
	SET_NETDEV_DEV(netdev, dev);
	es58x_dev->netdev[channel_idx] = netdev;
	es58x_init_priv(es58x_dev, es58x_priv(netdev), channel_idx);

	netdev->netdev_ops = &es58x_netdev_ops;
	netdev->flags |= IFF_ECHO;	/* We support local echo */

	ret = register_candev(netdev);
	if (ret)
		return ret;

	netdev_queue_set_dql_min_limit(netdev_get_tx_queue(netdev, 0),
				       es58x_dev->param->dql_min_limit);

	return ret;
}

/**
 * es58x_get_product_info() - Get the product information and print them.
 * @es58x_dev: ES58X device.
 *
 * Do a synchronous call to get the product information.
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_get_product_info(struct es58x_device *es58x_dev)
{
	struct usb_device *udev = es58x_dev->udev;
	const int es58x_prod_info_idx = 6;
	/* Empirical tests show a prod_info length of maximum 83,
	 * below should be more than enough.
	 */
	const size_t prod_info_len = 127;
	char *prod_info;
	int ret;

	prod_info = kmalloc(prod_info_len, GFP_KERNEL);
	if (!prod_info)
		return -ENOMEM;

	ret = usb_string(udev, es58x_prod_info_idx, prod_info, prod_info_len);
	if (ret < 0) {
		dev_err(es58x_dev->dev,
			"%s: Could not read the product info: %pe\n",
			__func__, ERR_PTR(ret));
		goto out_free;
	}
	if (ret >= prod_info_len - 1) {
		dev_warn(es58x_dev->dev,
			 "%s: Buffer is too small, result might be truncated\n",
			 __func__);
	}
	dev_info(es58x_dev->dev, "Product info: %s\n", prod_info);

 out_free:
	kfree(prod_info);
	return ret < 0 ? ret : 0;
}

/**
 * es58x_init_es58x_dev() - Initialize the ES58X device.
 * @intf: USB interface.
 * @p_es58x_dev: pointer to the address of the ES58X device.
 * @driver_info: Quirks of the device.
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_init_es58x_dev(struct usb_interface *intf,
				struct es58x_device **p_es58x_dev,
				kernel_ulong_t driver_info)
{
	struct device *dev = &intf->dev;
	struct es58x_device *es58x_dev;
	const struct es58x_parameters *param;
	const struct es58x_operators *ops;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep_in, *ep_out;
	int ret;

	dev_info(dev,
		 "Starting %s %s (Serial Number %s) driver version %s\n",
		 udev->manufacturer, udev->product, udev->serial, DRV_VERSION);

	ret = usb_find_common_endpoints(intf->cur_altsetting, &ep_in, &ep_out,
					NULL, NULL);
	if (ret)
		return ret;

	if (driver_info & ES58X_FD_FAMILY) {
		param = &es58x_fd_param;
		ops = &es58x_fd_ops;
	} else {
		param = &es581_4_param;
		ops = &es581_4_ops;
	}

	es58x_dev = kzalloc(es58x_sizeof_es58x_device(param), GFP_KERNEL);
	if (!es58x_dev)
		return -ENOMEM;

	es58x_dev->param = param;
	es58x_dev->ops = ops;
	es58x_dev->dev = dev;
	es58x_dev->udev = udev;

	if (driver_info & ES58X_DUAL_CHANNEL)
		es58x_dev->num_can_ch = 2;
	else
		es58x_dev->num_can_ch = 1;

	init_usb_anchor(&es58x_dev->rx_urbs);
	init_usb_anchor(&es58x_dev->tx_urbs_idle);
	init_usb_anchor(&es58x_dev->tx_urbs_busy);
	atomic_set(&es58x_dev->tx_urbs_idle_cnt, 0);
	atomic_set(&es58x_dev->opened_channel_cnt, 0);
	usb_set_intfdata(intf, es58x_dev);

	es58x_dev->rx_pipe = usb_rcvbulkpipe(es58x_dev->udev,
					     ep_in->bEndpointAddress);
	es58x_dev->tx_pipe = usb_sndbulkpipe(es58x_dev->udev,
					     ep_out->bEndpointAddress);
	es58x_dev->rx_max_packet_size = le16_to_cpu(ep_in->wMaxPacketSize);

	*p_es58x_dev = es58x_dev;

	return 0;
}

/**
 * es58x_probe() - Initialize the USB device.
 * @intf: USB interface.
 * @id: USB device ID.
 *
 * Return: zero on success, -ENODEV if the interface is not supported
 * or errno when any other error occurs.
 */
static int es58x_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct es58x_device *es58x_dev;
	int ch_idx, ret;

	ret = es58x_init_es58x_dev(intf, &es58x_dev, id->driver_info);
	if (ret)
		return ret;

	ret = es58x_get_product_info(es58x_dev);
	if (ret)
		goto cleanup_es58x_dev;

	for (ch_idx = 0; ch_idx < es58x_dev->num_can_ch; ch_idx++) {
		ret = es58x_init_netdev(es58x_dev, ch_idx);
		if (ret)
			goto cleanup_candev;
	}

	return ret;

 cleanup_candev:
	for (ch_idx = 0; ch_idx < es58x_dev->num_can_ch; ch_idx++)
		if (es58x_dev->netdev[ch_idx]) {
			unregister_candev(es58x_dev->netdev[ch_idx]);
			free_candev(es58x_dev->netdev[ch_idx]);
		}
 cleanup_es58x_dev:
	kfree(es58x_dev);

	return ret;
}

/**
 * es58x_disconnect() - Disconnect the USB device.
 * @intf: USB interface
 *
 * Called by the usb core when driver is unloaded or device is
 * removed.
 */
static void es58x_disconnect(struct usb_interface *intf)
{
	struct es58x_device *es58x_dev = usb_get_intfdata(intf);
	struct net_device *netdev;
	int i;

	dev_info(&intf->dev, "Disconnecting %s %s\n",
		 es58x_dev->udev->manufacturer, es58x_dev->udev->product);

	for (i = 0; i < es58x_dev->num_can_ch; i++) {
		netdev = es58x_dev->netdev[i];
		if (!netdev)
			continue;
		unregister_candev(netdev);
		es58x_dev->netdev[i] = NULL;
		free_candev(netdev);
	}

	es58x_free_urbs(es58x_dev);

	kfree(es58x_dev);
	usb_set_intfdata(intf, NULL);
}

static struct usb_driver es58x_driver = {
	.name = ES58X_MODULE_NAME,
	.probe = es58x_probe,
	.disconnect = es58x_disconnect,
	.id_table = es58x_id_table
};

module_usb_driver(es58x_driver);
