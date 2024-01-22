// SPDX-License-Identifier: GPL-2.0
/*
 * Beagleplay Linux Driver for Greybus
 *
 * Copyright (c) 2023 Ayush Singh <ayushdevel1325@gmail.com>
 * Copyright (c) 2023 BeagleBoard.org Foundation
 */

#include <linux/gfp.h>
#include <linux/greybus.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/serdev.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/greybus/hd.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/crc-ccitt.h>
#include <linux/circ_buf.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define RX_HDLC_PAYLOAD 256
#define CRC_LEN 2
#define MAX_RX_HDLC (1 + RX_HDLC_PAYLOAD + CRC_LEN)
#define TX_CIRC_BUF_SIZE 1024

#define ADDRESS_GREYBUS 0x01
#define ADDRESS_DBG 0x02
#define ADDRESS_CONTROL 0x03

#define HDLC_FRAME 0x7E
#define HDLC_ESC 0x7D
#define HDLC_XOR 0x20

#define CONTROL_SVC_START 0x01
#define CONTROL_SVC_STOP 0x02

/* The maximum number of CPorts supported by Greybus Host Device */
#define GB_MAX_CPORTS 32

/**
 * struct gb_beagleplay - BeaglePlay Greybus driver
 *
 * @sd: underlying serdev device
 *
 * @gb_hd: greybus host device
 *
 * @tx_work: hdlc transmit work
 * @tx_producer_lock: hdlc transmit data producer lock. acquired when appending data to buffer.
 * @tx_consumer_lock: hdlc transmit data consumer lock. acquired when sending data over uart.
 * @tx_circ_buf: hdlc transmit circular buffer.
 * @tx_crc: hdlc transmit crc-ccitt fcs
 *
 * @rx_buffer_len: length of receive buffer filled.
 * @rx_buffer: hdlc frame receive buffer
 * @rx_in_esc: hdlc rx flag to indicate ESC frame
 */
struct gb_beagleplay {
	struct serdev_device *sd;

	struct gb_host_device *gb_hd;

	struct work_struct tx_work;
	spinlock_t tx_producer_lock;
	spinlock_t tx_consumer_lock;
	struct circ_buf tx_circ_buf;
	u16 tx_crc;

	u16 rx_buffer_len;
	bool rx_in_esc;
	u8 rx_buffer[MAX_RX_HDLC];
};

/**
 * struct hdlc_payload - Structure to represent part of HDCL frame payload data.
 *
 * @len: buffer length in bytes
 * @buf: payload buffer
 */
struct hdlc_payload {
	u16 len;
	void *buf;
};

/**
 * struct hdlc_greybus_frame - Structure to represent greybus HDLC frame payload
 *
 * @cport: cport id
 * @hdr: greybus operation header
 * @payload: greybus message payload
 *
 * The HDLC payload sent over UART for greybus address has cport preappended to greybus message
 */
struct hdlc_greybus_frame {
	__le16 cport;
	struct gb_operation_msg_hdr hdr;
	u8 payload[];
} __packed;

static void hdlc_rx_greybus_frame(struct gb_beagleplay *bg, u8 *buf, u16 len)
{
	struct hdlc_greybus_frame *gb_frame = (struct hdlc_greybus_frame *)buf;
	u16 cport_id = le16_to_cpu(gb_frame->cport);
	u16 gb_msg_len = le16_to_cpu(gb_frame->hdr.size);

	dev_dbg(&bg->sd->dev, "Greybus Operation %u type %X cport %u status %u received",
		gb_frame->hdr.operation_id, gb_frame->hdr.type, cport_id, gb_frame->hdr.result);

	greybus_data_rcvd(bg->gb_hd, cport_id, (u8 *)&gb_frame->hdr, gb_msg_len);
}

static void hdlc_rx_dbg_frame(const struct gb_beagleplay *bg, const char *buf, u16 len)
{
	dev_dbg(&bg->sd->dev, "CC1352 Log: %.*s", (int)len, buf);
}

/**
 * hdlc_write() - Consume HDLC Buffer.
 * @bg: beagleplay greybus driver
 *
 * Assumes that consumer lock has been acquired.
 */
static void hdlc_write(struct gb_beagleplay *bg)
{
	int written;
	/* Start consuming HDLC data */
	int head = smp_load_acquire(&bg->tx_circ_buf.head);
	int tail = bg->tx_circ_buf.tail;
	int count = CIRC_CNT_TO_END(head, tail, TX_CIRC_BUF_SIZE);
	const unsigned char *buf = &bg->tx_circ_buf.buf[tail];

	if (count > 0) {
		written = serdev_device_write_buf(bg->sd, buf, count);

		/* Finish consuming HDLC data */
		smp_store_release(&bg->tx_circ_buf.tail, (tail + written) & (TX_CIRC_BUF_SIZE - 1));
	}
}

/**
 * hdlc_append() - Queue HDLC data for sending.
 * @bg: beagleplay greybus driver
 * @value: hdlc byte to transmit
 *
 * Assumes that producer lock as been acquired.
 */
static void hdlc_append(struct gb_beagleplay *bg, u8 value)
{
	int tail, head = bg->tx_circ_buf.head;

	while (true) {
		tail = READ_ONCE(bg->tx_circ_buf.tail);

		if (CIRC_SPACE(head, tail, TX_CIRC_BUF_SIZE) >= 1) {
			bg->tx_circ_buf.buf[head] = value;

			/* Finish producing HDLC byte */
			smp_store_release(&bg->tx_circ_buf.head,
					  (head + 1) & (TX_CIRC_BUF_SIZE - 1));
			return;
		}
		dev_warn(&bg->sd->dev, "Tx circ buf full");
		usleep_range(3000, 5000);
	}
}

static void hdlc_append_escaped(struct gb_beagleplay *bg, u8 value)
{
	if (value == HDLC_FRAME || value == HDLC_ESC) {
		hdlc_append(bg, HDLC_ESC);
		value ^= HDLC_XOR;
	}
	hdlc_append(bg, value);
}

static void hdlc_append_tx_frame(struct gb_beagleplay *bg)
{
	bg->tx_crc = 0xFFFF;
	hdlc_append(bg, HDLC_FRAME);
}

static void hdlc_append_tx_u8(struct gb_beagleplay *bg, u8 value)
{
	bg->tx_crc = crc_ccitt(bg->tx_crc, &value, 1);
	hdlc_append_escaped(bg, value);
}

static void hdlc_append_tx_buf(struct gb_beagleplay *bg, const u8 *buf, u16 len)
{
	size_t i;

	for (i = 0; i < len; i++)
		hdlc_append_tx_u8(bg, buf[i]);
}

static void hdlc_append_tx_crc(struct gb_beagleplay *bg)
{
	bg->tx_crc ^= 0xffff;
	hdlc_append_escaped(bg, bg->tx_crc & 0xff);
	hdlc_append_escaped(bg, (bg->tx_crc >> 8) & 0xff);
}

static void hdlc_transmit(struct work_struct *work)
{
	struct gb_beagleplay *bg = container_of(work, struct gb_beagleplay, tx_work);

	spin_lock_bh(&bg->tx_consumer_lock);
	hdlc_write(bg);
	spin_unlock_bh(&bg->tx_consumer_lock);
}

static void hdlc_tx_frames(struct gb_beagleplay *bg, u8 address, u8 control,
			   const struct hdlc_payload payloads[], size_t count)
{
	size_t i;

	spin_lock(&bg->tx_producer_lock);

	hdlc_append_tx_frame(bg);
	hdlc_append_tx_u8(bg, address);
	hdlc_append_tx_u8(bg, control);

	for (i = 0; i < count; ++i)
		hdlc_append_tx_buf(bg, payloads[i].buf, payloads[i].len);

	hdlc_append_tx_crc(bg);
	hdlc_append_tx_frame(bg);

	spin_unlock(&bg->tx_producer_lock);

	schedule_work(&bg->tx_work);
}

static void hdlc_tx_s_frame_ack(struct gb_beagleplay *bg)
{
	hdlc_tx_frames(bg, bg->rx_buffer[0], (bg->rx_buffer[1] >> 1) & 0x7, NULL, 0);
}

static void hdlc_rx_frame(struct gb_beagleplay *bg)
{
	u16 crc, len;
	u8 ctrl, *buf;
	u8 address = bg->rx_buffer[0];

	crc = crc_ccitt(0xffff, bg->rx_buffer, bg->rx_buffer_len);
	if (crc != 0xf0b8) {
		dev_warn_ratelimited(&bg->sd->dev, "CRC failed from %02x: 0x%04x", address, crc);
		return;
	}

	ctrl = bg->rx_buffer[1];
	buf = &bg->rx_buffer[2];
	len = bg->rx_buffer_len - 4;

	/* I-Frame, send S-Frame ACK */
	if ((ctrl & 1) == 0)
		hdlc_tx_s_frame_ack(bg);

	switch (address) {
	case ADDRESS_DBG:
		hdlc_rx_dbg_frame(bg, buf, len);
		break;
	case ADDRESS_GREYBUS:
		hdlc_rx_greybus_frame(bg, buf, len);
		break;
	default:
		dev_warn_ratelimited(&bg->sd->dev, "unknown frame %u", address);
	}
}

static ssize_t hdlc_rx(struct gb_beagleplay *bg, const u8 *data, size_t count)
{
	size_t i;
	u8 c;

	for (i = 0; i < count; ++i) {
		c = data[i];

		switch (c) {
		case HDLC_FRAME:
			if (bg->rx_buffer_len)
				hdlc_rx_frame(bg);

			bg->rx_buffer_len = 0;
			break;
		case HDLC_ESC:
			bg->rx_in_esc = true;
			break;
		default:
			if (bg->rx_in_esc) {
				c ^= 0x20;
				bg->rx_in_esc = false;
			}

			if (bg->rx_buffer_len < MAX_RX_HDLC) {
				bg->rx_buffer[bg->rx_buffer_len] = c;
				bg->rx_buffer_len++;
			} else {
				dev_err_ratelimited(&bg->sd->dev, "RX Buffer Overflow");
				bg->rx_buffer_len = 0;
			}
		}
	}

	return count;
}

static int hdlc_init(struct gb_beagleplay *bg)
{
	INIT_WORK(&bg->tx_work, hdlc_transmit);
	spin_lock_init(&bg->tx_producer_lock);
	spin_lock_init(&bg->tx_consumer_lock);
	bg->tx_circ_buf.head = 0;
	bg->tx_circ_buf.tail = 0;

	bg->tx_circ_buf.buf = devm_kmalloc(&bg->sd->dev, TX_CIRC_BUF_SIZE, GFP_KERNEL);
	if (!bg->tx_circ_buf.buf)
		return -ENOMEM;

	bg->rx_buffer_len = 0;
	bg->rx_in_esc = false;

	return 0;
}

static void hdlc_deinit(struct gb_beagleplay *bg)
{
	flush_work(&bg->tx_work);
}

static ssize_t gb_tty_receive(struct serdev_device *sd, const u8 *data,
			      size_t count)
{
	struct gb_beagleplay *bg = serdev_device_get_drvdata(sd);

	return hdlc_rx(bg, data, count);
}

static void gb_tty_wakeup(struct serdev_device *serdev)
{
	struct gb_beagleplay *bg = serdev_device_get_drvdata(serdev);

	schedule_work(&bg->tx_work);
}

static struct serdev_device_ops gb_beagleplay_ops = {
	.receive_buf = gb_tty_receive,
	.write_wakeup = gb_tty_wakeup,
};

/**
 * gb_message_send() - Send greybus message using HDLC over UART
 *
 * @hd: pointer to greybus host device
 * @cport: AP cport where message originates
 * @msg: greybus message to send
 * @mask: gfp mask
 *
 * Greybus HDLC frame has the following payload:
 * 1. le16 cport
 * 2. gb_operation_msg_hdr msg_header
 * 3. u8 *msg_payload
 */
static int gb_message_send(struct gb_host_device *hd, u16 cport, struct gb_message *msg, gfp_t mask)
{
	struct gb_beagleplay *bg = dev_get_drvdata(&hd->dev);
	struct hdlc_payload payloads[3];
	__le16 cport_id = cpu_to_le16(cport);

	dev_dbg(&hd->dev, "Sending greybus message with Operation %u, Type: %X on Cport %u",
		msg->header->operation_id, msg->header->type, cport);

	if (le16_to_cpu(msg->header->size) > RX_HDLC_PAYLOAD)
		return dev_err_probe(&hd->dev, -E2BIG, "Greybus message too big");

	payloads[0].buf = &cport_id;
	payloads[0].len = sizeof(cport_id);
	payloads[1].buf = msg->header;
	payloads[1].len = sizeof(*msg->header);
	payloads[2].buf = msg->payload;
	payloads[2].len = msg->payload_size;

	hdlc_tx_frames(bg, ADDRESS_GREYBUS, 0x03, payloads, 3);
	greybus_message_sent(bg->gb_hd, msg, 0);

	return 0;
}

static void gb_message_cancel(struct gb_message *message)
{
}

static struct gb_hd_driver gb_hdlc_driver = { .message_send = gb_message_send,
					      .message_cancel = gb_message_cancel };

static void gb_beagleplay_start_svc(struct gb_beagleplay *bg)
{
	const u8 command = CONTROL_SVC_START;
	const struct hdlc_payload payload = { .len = 1, .buf = (void *)&command };

	hdlc_tx_frames(bg, ADDRESS_CONTROL, 0x03, &payload, 1);
}

static void gb_beagleplay_stop_svc(struct gb_beagleplay *bg)
{
	const u8 command = CONTROL_SVC_STOP;
	const struct hdlc_payload payload = { .len = 1, .buf = (void *)&command };

	hdlc_tx_frames(bg, ADDRESS_CONTROL, 0x03, &payload, 1);
}

static void gb_greybus_deinit(struct gb_beagleplay *bg)
{
	gb_hd_del(bg->gb_hd);
	gb_hd_put(bg->gb_hd);
}

static int gb_greybus_init(struct gb_beagleplay *bg)
{
	int ret;

	bg->gb_hd = gb_hd_create(&gb_hdlc_driver, &bg->sd->dev, TX_CIRC_BUF_SIZE, GB_MAX_CPORTS);
	if (IS_ERR(bg->gb_hd)) {
		dev_err(&bg->sd->dev, "Failed to create greybus host device");
		return PTR_ERR(bg->gb_hd);
	}

	ret = gb_hd_add(bg->gb_hd);
	if (ret) {
		dev_err(&bg->sd->dev, "Failed to add greybus host device");
		goto free_gb_hd;
	}
	dev_set_drvdata(&bg->gb_hd->dev, bg);

	return 0;

free_gb_hd:
	gb_greybus_deinit(bg);
	return ret;
}

static void gb_serdev_deinit(struct gb_beagleplay *bg)
{
	serdev_device_close(bg->sd);
}

static int gb_serdev_init(struct gb_beagleplay *bg)
{
	int ret;

	serdev_device_set_drvdata(bg->sd, bg);
	serdev_device_set_client_ops(bg->sd, &gb_beagleplay_ops);
	ret = serdev_device_open(bg->sd);
	if (ret)
		return dev_err_probe(&bg->sd->dev, ret, "Unable to open serial device");

	serdev_device_set_baudrate(bg->sd, 115200);
	serdev_device_set_flow_control(bg->sd, false);

	return 0;
}

static int gb_beagleplay_probe(struct serdev_device *serdev)
{
	int ret = 0;
	struct gb_beagleplay *bg;

	bg = devm_kmalloc(&serdev->dev, sizeof(*bg), GFP_KERNEL);
	if (!bg)
		return -ENOMEM;

	bg->sd = serdev;
	ret = gb_serdev_init(bg);
	if (ret)
		return ret;

	ret = hdlc_init(bg);
	if (ret)
		goto free_serdev;

	ret = gb_greybus_init(bg);
	if (ret)
		goto free_hdlc;

	gb_beagleplay_start_svc(bg);

	return 0;

free_hdlc:
	hdlc_deinit(bg);
free_serdev:
	gb_serdev_deinit(bg);
	return ret;
}

static void gb_beagleplay_remove(struct serdev_device *serdev)
{
	struct gb_beagleplay *bg = serdev_device_get_drvdata(serdev);

	gb_greybus_deinit(bg);
	gb_beagleplay_stop_svc(bg);
	hdlc_deinit(bg);
	gb_serdev_deinit(bg);
}

static const struct of_device_id gb_beagleplay_of_match[] = {
	{
		.compatible = "ti,cc1352p7",
	},
	{},
};
MODULE_DEVICE_TABLE(of, gb_beagleplay_of_match);

static struct serdev_device_driver gb_beagleplay_driver = {
	.probe = gb_beagleplay_probe,
	.remove = gb_beagleplay_remove,
	.driver = {
		.name = "gb_beagleplay",
		.of_match_table = gb_beagleplay_of_match,
	},
};

module_serdev_device_driver(gb_beagleplay_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ayush Singh <ayushdevel1325@gmail.com>");
MODULE_DESCRIPTION("A Greybus driver for BeaglePlay");
