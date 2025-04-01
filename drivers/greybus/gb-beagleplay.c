// SPDX-License-Identifier: GPL-2.0
/*
 * Beagleplay Linux Driver for Greybus
 *
 * Copyright (c) 2023 Ayush Singh <ayushdevel1325@gmail.com>
 * Copyright (c) 2023 BeagleBoard.org Foundation
 */

#include <linux/unaligned.h>
#include <linux/crc32.h>
#include <linux/gpio/consumer.h>
#include <linux/firmware.h>
#include <linux/greybus.h>
#include <linux/serdev.h>
#include <linux/crc-ccitt.h>
#include <linux/circ_buf.h>

#define CC1352_FIRMWARE_SIZE (704 * 1024)
#define CC1352_BOOTLOADER_TIMEOUT 2000
#define CC1352_BOOTLOADER_ACK 0xcc
#define CC1352_BOOTLOADER_NACK 0x33

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
 *
 * @fwl: underlying firmware upload device
 * @bootloader_backdoor_gpio: cc1352p7 boot gpio
 * @rst_gpio: cc1352p7 reset gpio
 * @flashing_mode: flag to indicate that flashing is currently in progress
 * @fwl_ack_com: completion to signal an Ack/Nack
 * @fwl_ack: Ack/Nack byte received
 * @fwl_cmd_response_com: completion to signal a bootloader command response
 * @fwl_cmd_response: bootloader command response data
 * @fwl_crc32: crc32 of firmware to flash
 * @fwl_reset_addr: flag to indicate if we need to send COMMAND_DOWNLOAD again
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

	struct fw_upload *fwl;
	struct gpio_desc *bootloader_backdoor_gpio;
	struct gpio_desc *rst_gpio;
	bool flashing_mode;
	struct completion fwl_ack_com;
	u8 fwl_ack;
	struct completion fwl_cmd_response_com;
	u32 fwl_cmd_response;
	u32 fwl_crc32;
	bool fwl_reset_addr;
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

/**
 * enum cc1352_bootloader_cmd: CC1352 Bootloader Commands
 *
 * @COMMAND_DOWNLOAD: Prepares flash programming
 * @COMMAND_GET_STATUS: Returns the status of the last command that was  issued
 * @COMMAND_SEND_DATA: Transfers data and programs flash
 * @COMMAND_RESET: Performs a system reset
 * @COMMAND_CRC32: Calculates CRC32 over a specified memory area
 * @COMMAND_BANK_ERASE: Performs an erase of all of the customer-accessible
 *                      flash sectors not protected by FCFG1 and CCFG
 *                      writeprotect bits.
 *
 * CC1352 Bootloader serial bus commands
 */
enum cc1352_bootloader_cmd {
	COMMAND_DOWNLOAD = 0x21,
	COMMAND_GET_STATUS = 0x23,
	COMMAND_SEND_DATA = 0x24,
	COMMAND_RESET = 0x25,
	COMMAND_CRC32 = 0x27,
	COMMAND_BANK_ERASE = 0x2c,
};

/**
 * enum cc1352_bootloader_status: CC1352 Bootloader COMMAND_GET_STATUS response
 *
 * @COMMAND_RET_SUCCESS: Status for successful command
 * @COMMAND_RET_UNKNOWN_CMD: Status for unknown command
 * @COMMAND_RET_INVALID_CMD: Status for invalid command (in other words,
 *                           incorrect packet size)
 * @COMMAND_RET_INVALID_ADR: Status for invalid input address
 * @COMMAND_RET_FLASH_FAIL: Status for failing flash erase or program operation
 */
enum cc1352_bootloader_status {
	COMMAND_RET_SUCCESS = 0x40,
	COMMAND_RET_UNKNOWN_CMD = 0x41,
	COMMAND_RET_INVALID_CMD = 0x42,
	COMMAND_RET_INVALID_ADR = 0x43,
	COMMAND_RET_FLASH_FAIL = 0x44,
};

/**
 * struct cc1352_bootloader_packet: CC1352 Bootloader Request Packet
 *
 * @len: length of packet + optional request data
 * @checksum: 8-bit checksum excluding len
 * @cmd: bootloader command
 */
struct cc1352_bootloader_packet {
	u8 len;
	u8 checksum;
	u8 cmd;
} __packed;

#define CC1352_BOOTLOADER_PKT_MAX_SIZE \
	(U8_MAX - sizeof(struct cc1352_bootloader_packet))

/**
 * struct cc1352_bootloader_download_cmd_data: CC1352 Bootloader COMMAND_DOWNLOAD request data
 *
 * @addr: address to start programming data into
 * @size: size of data that will be sent
 */
struct cc1352_bootloader_download_cmd_data {
	__be32 addr;
	__be32 size;
} __packed;

/**
 * struct cc1352_bootloader_crc32_cmd_data: CC1352 Bootloader COMMAND_CRC32 request data
 *
 * @addr: address where crc32 calculation starts
 * @size: number of bytes comprised by crc32 calculation
 * @read_repeat: number of read repeats for each data location
 */
struct cc1352_bootloader_crc32_cmd_data {
	__be32 addr;
	__be32 size;
	__be32 read_repeat;
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

static size_t hdlc_rx(struct gb_beagleplay *bg, const u8 *data, size_t count)
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

/**
 * csum8: Calculate 8-bit checksum on data
 *
 * @data: bytes to calculate 8-bit checksum of
 * @size: number of bytes
 * @base: starting value for checksum
 */
static u8 csum8(const u8 *data, size_t size, u8 base)
{
	size_t i;
	u8 sum = base;

	for (i = 0; i < size; ++i)
		sum += data[i];

	return sum;
}

static void cc1352_bootloader_send_ack(struct gb_beagleplay *bg)
{
	static const u8 ack[] = { 0x00, CC1352_BOOTLOADER_ACK };

	serdev_device_write_buf(bg->sd, ack, sizeof(ack));
}

static void cc1352_bootloader_send_nack(struct gb_beagleplay *bg)
{
	static const u8 nack[] = { 0x00, CC1352_BOOTLOADER_NACK };

	serdev_device_write_buf(bg->sd, nack, sizeof(nack));
}

/**
 * cc1352_bootloader_pkt_rx: Process a CC1352 Bootloader Packet
 *
 * @bg: beagleplay greybus driver
 * @data: packet buffer
 * @count: packet buffer size
 *
 * @return: number of bytes processed
 *
 * Here are the steps to successfully receive a packet from cc1352 bootloader
 * according to the docs:
 * 1. Wait for nonzero data to be returned from the device. This is important
 *    as the device may send zero bytes between a sent and a received data
 *    packet. The first nonzero byte received is the size of the packet that is
 *    being received.
 * 2. Read the next byte, which is the checksum for the packet.
 * 3. Read the data bytes from the device. During the data phase, packet size
 *    minus 2 bytes is sent.
 * 4. Calculate the checksum of the data bytes and verify it matches the
 *    checksum received in the packet.
 * 5. Send an acknowledge byte or a not-acknowledge byte to the device to
 *    indicate the successful or unsuccessful reception of the packet.
 */
static int cc1352_bootloader_pkt_rx(struct gb_beagleplay *bg, const u8 *data,
				    size_t count)
{
	bool is_valid = false;

	switch (data[0]) {
	/* Skip 0x00 bytes.  */
	case 0x00:
		return 1;
	case CC1352_BOOTLOADER_ACK:
	case CC1352_BOOTLOADER_NACK:
		WRITE_ONCE(bg->fwl_ack, data[0]);
		complete(&bg->fwl_ack_com);
		return 1;
	case 3:
		if (count < 3)
			return 0;
		is_valid = data[1] == data[2];
		WRITE_ONCE(bg->fwl_cmd_response, (u32)data[2]);
		break;
	case 6:
		if (count < 6)
			return 0;
		is_valid = csum8(&data[2], sizeof(__be32), 0) == data[1];
		WRITE_ONCE(bg->fwl_cmd_response, get_unaligned_be32(&data[2]));
		break;
	default:
		return -EINVAL;
	}

	if (is_valid) {
		cc1352_bootloader_send_ack(bg);
		complete(&bg->fwl_cmd_response_com);
	} else {
		dev_warn(&bg->sd->dev,
			 "Dropping bootloader packet with invalid checksum");
		cc1352_bootloader_send_nack(bg);
	}

	return data[0];
}

static size_t cc1352_bootloader_rx(struct gb_beagleplay *bg, const u8 *data,
				   size_t count)
{
	int ret;
	size_t off = 0;

	memcpy(bg->rx_buffer + bg->rx_buffer_len, data, count);
	bg->rx_buffer_len += count;

	do {
		ret = cc1352_bootloader_pkt_rx(bg, bg->rx_buffer + off,
					       bg->rx_buffer_len - off);
		if (ret < 0)
			return dev_err_probe(&bg->sd->dev, ret,
					     "Invalid Packet");
		off += ret;
	} while (ret > 0 && off < count);

	bg->rx_buffer_len -= off;
	memmove(bg->rx_buffer, bg->rx_buffer + off, bg->rx_buffer_len);

	return count;
}

static size_t gb_tty_receive(struct serdev_device *sd, const u8 *data,
			     size_t count)
{
	struct gb_beagleplay *bg = serdev_device_get_drvdata(sd);

	if (READ_ONCE(bg->flashing_mode))
		return cc1352_bootloader_rx(bg, data, count);

	return hdlc_rx(bg, data, count);
}

static void gb_tty_wakeup(struct serdev_device *serdev)
{
	struct gb_beagleplay *bg = serdev_device_get_drvdata(serdev);

	if (!READ_ONCE(bg->flashing_mode))
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

static int cc1352_bootloader_wait_for_ack(struct gb_beagleplay *bg)
{
	int ret;

	ret = wait_for_completion_timeout(
		&bg->fwl_ack_com, msecs_to_jiffies(CC1352_BOOTLOADER_TIMEOUT));
	if (ret < 0)
		return dev_err_probe(&bg->sd->dev, ret,
				     "Failed to acquire ack semaphore");

	switch (READ_ONCE(bg->fwl_ack)) {
	case CC1352_BOOTLOADER_ACK:
		return 0;
	case CC1352_BOOTLOADER_NACK:
		return -EAGAIN;
	default:
		return -EINVAL;
	}
}

static int cc1352_bootloader_sync(struct gb_beagleplay *bg)
{
	static const u8 sync_bytes[] = { 0x55, 0x55 };

	serdev_device_write_buf(bg->sd, sync_bytes, sizeof(sync_bytes));
	return cc1352_bootloader_wait_for_ack(bg);
}

static int cc1352_bootloader_get_status(struct gb_beagleplay *bg)
{
	int ret;
	static const struct cc1352_bootloader_packet pkt = {
		.len = sizeof(pkt),
		.checksum = COMMAND_GET_STATUS,
		.cmd = COMMAND_GET_STATUS
	};

	serdev_device_write_buf(bg->sd, (const u8 *)&pkt, sizeof(pkt));
	ret = cc1352_bootloader_wait_for_ack(bg);
	if (ret < 0)
		return ret;

	ret = wait_for_completion_timeout(
		&bg->fwl_cmd_response_com,
		msecs_to_jiffies(CC1352_BOOTLOADER_TIMEOUT));
	if (ret < 0)
		return dev_err_probe(&bg->sd->dev, ret,
				     "Failed to acquire last status semaphore");

	switch (READ_ONCE(bg->fwl_cmd_response)) {
	case COMMAND_RET_SUCCESS:
		return 0;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cc1352_bootloader_erase(struct gb_beagleplay *bg)
{
	int ret;
	static const struct cc1352_bootloader_packet pkt = {
		.len = sizeof(pkt),
		.checksum = COMMAND_BANK_ERASE,
		.cmd = COMMAND_BANK_ERASE
	};

	serdev_device_write_buf(bg->sd, (const u8 *)&pkt, sizeof(pkt));

	ret = cc1352_bootloader_wait_for_ack(bg);
	if (ret < 0)
		return ret;

	return cc1352_bootloader_get_status(bg);
}

static int cc1352_bootloader_reset(struct gb_beagleplay *bg)
{
	static const struct cc1352_bootloader_packet pkt = {
		.len = sizeof(pkt),
		.checksum = COMMAND_RESET,
		.cmd = COMMAND_RESET
	};

	serdev_device_write_buf(bg->sd, (const u8 *)&pkt, sizeof(pkt));

	return cc1352_bootloader_wait_for_ack(bg);
}

/**
 * cc1352_bootloader_empty_pkt: Calculate the number of empty bytes in the current packet
 *
 * @data: packet bytes array to check
 * @size: number of bytes in array
 */
static size_t cc1352_bootloader_empty_pkt(const u8 *data, size_t size)
{
	size_t i;

	for (i = 0; i < size && data[i] == 0xff; ++i)
		continue;

	return i;
}

static int cc1352_bootloader_crc32(struct gb_beagleplay *bg, u32 *crc32)
{
	int ret;
	static const struct cc1352_bootloader_crc32_cmd_data cmd_data = {
		.addr = 0, .size = cpu_to_be32(704 * 1024), .read_repeat = 0
	};
	const struct cc1352_bootloader_packet pkt = {
		.len = sizeof(pkt) + sizeof(cmd_data),
		.checksum = csum8((const void *)&cmd_data, sizeof(cmd_data),
				  COMMAND_CRC32),
		.cmd = COMMAND_CRC32
	};

	serdev_device_write_buf(bg->sd, (const u8 *)&pkt, sizeof(pkt));
	serdev_device_write_buf(bg->sd, (const u8 *)&cmd_data,
				sizeof(cmd_data));

	ret = cc1352_bootloader_wait_for_ack(bg);
	if (ret < 0)
		return ret;

	ret = wait_for_completion_timeout(
		&bg->fwl_cmd_response_com,
		msecs_to_jiffies(CC1352_BOOTLOADER_TIMEOUT));
	if (ret < 0)
		return dev_err_probe(&bg->sd->dev, ret,
				     "Failed to acquire last status semaphore");

	*crc32 = READ_ONCE(bg->fwl_cmd_response);

	return 0;
}

static int cc1352_bootloader_download(struct gb_beagleplay *bg, u32 size,
				      u32 addr)
{
	int ret;
	const struct cc1352_bootloader_download_cmd_data cmd_data = {
		.addr = cpu_to_be32(addr),
		.size = cpu_to_be32(size),
	};
	const struct cc1352_bootloader_packet pkt = {
		.len = sizeof(pkt) + sizeof(cmd_data),
		.checksum = csum8((const void *)&cmd_data, sizeof(cmd_data),
				  COMMAND_DOWNLOAD),
		.cmd = COMMAND_DOWNLOAD
	};

	serdev_device_write_buf(bg->sd, (const u8 *)&pkt, sizeof(pkt));
	serdev_device_write_buf(bg->sd, (const u8 *)&cmd_data,
				sizeof(cmd_data));

	ret = cc1352_bootloader_wait_for_ack(bg);
	if (ret < 0)
		return ret;

	return cc1352_bootloader_get_status(bg);
}

static int cc1352_bootloader_send_data(struct gb_beagleplay *bg, const u8 *data,
				       size_t size)
{
	int ret, rem = min(size, CC1352_BOOTLOADER_PKT_MAX_SIZE);
	const struct cc1352_bootloader_packet pkt = {
		.len = sizeof(pkt) + rem,
		.checksum = csum8(data, rem, COMMAND_SEND_DATA),
		.cmd = COMMAND_SEND_DATA
	};

	serdev_device_write_buf(bg->sd, (const u8 *)&pkt, sizeof(pkt));
	serdev_device_write_buf(bg->sd, data, rem);

	ret = cc1352_bootloader_wait_for_ack(bg);
	if (ret < 0)
		return ret;

	ret = cc1352_bootloader_get_status(bg);
	if (ret < 0)
		return ret;

	return rem;
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

static enum fw_upload_err cc1352_prepare(struct fw_upload *fw_upload,
					 const u8 *data, u32 size)
{
	int ret;
	u32 curr_crc32;
	struct gb_beagleplay *bg = fw_upload->dd_handle;

	dev_info(&bg->sd->dev, "CC1352 Start Flashing...");

	if (size != CC1352_FIRMWARE_SIZE)
		return FW_UPLOAD_ERR_INVALID_SIZE;

	/* Might involve network calls */
	gb_greybus_deinit(bg);
	msleep(5 * MSEC_PER_SEC);

	gb_beagleplay_stop_svc(bg);
	msleep(200);
	flush_work(&bg->tx_work);

	serdev_device_wait_until_sent(bg->sd, CC1352_BOOTLOADER_TIMEOUT);

	WRITE_ONCE(bg->flashing_mode, true);

	gpiod_direction_output(bg->bootloader_backdoor_gpio, 0);
	gpiod_direction_output(bg->rst_gpio, 0);
	msleep(200);

	gpiod_set_value(bg->rst_gpio, 1);
	msleep(200);

	gpiod_set_value(bg->bootloader_backdoor_gpio, 1);
	msleep(200);

	gpiod_direction_input(bg->bootloader_backdoor_gpio);
	gpiod_direction_input(bg->rst_gpio);

	ret = cc1352_bootloader_sync(bg);
	if (ret < 0)
		return dev_err_probe(&bg->sd->dev, FW_UPLOAD_ERR_HW_ERROR,
				     "Failed to sync");

	ret = cc1352_bootloader_crc32(bg, &curr_crc32);
	if (ret < 0)
		return dev_err_probe(&bg->sd->dev, FW_UPLOAD_ERR_HW_ERROR,
				     "Failed to fetch crc32");

	bg->fwl_crc32 = crc32(0xffffffff, data, size) ^ 0xffffffff;

	/* Check if attempting to reflash same firmware */
	if (bg->fwl_crc32 == curr_crc32) {
		dev_warn(&bg->sd->dev, "Skipping reflashing same image");
		cc1352_bootloader_reset(bg);
		WRITE_ONCE(bg->flashing_mode, false);
		msleep(200);
		if (gb_greybus_init(bg) < 0)
			return dev_err_probe(&bg->sd->dev, FW_UPLOAD_ERR_RW_ERROR,
					     "Failed to initialize greybus");
		gb_beagleplay_start_svc(bg);
		return FW_UPLOAD_ERR_FW_INVALID;
	}

	ret = cc1352_bootloader_erase(bg);
	if (ret < 0)
		return dev_err_probe(&bg->sd->dev, FW_UPLOAD_ERR_HW_ERROR,
				     "Failed to erase");

	bg->fwl_reset_addr = true;

	return FW_UPLOAD_ERR_NONE;
}

static void cc1352_cleanup(struct fw_upload *fw_upload)
{
	struct gb_beagleplay *bg = fw_upload->dd_handle;

	WRITE_ONCE(bg->flashing_mode, false);
}

static enum fw_upload_err cc1352_write(struct fw_upload *fw_upload,
				       const u8 *data, u32 offset, u32 size,
				       u32 *written)
{
	int ret;
	size_t empty_bytes;
	struct gb_beagleplay *bg = fw_upload->dd_handle;

	/* Skip 0xff packets. Significant performance improvement */
	empty_bytes = cc1352_bootloader_empty_pkt(data + offset, size);
	if (empty_bytes >= CC1352_BOOTLOADER_PKT_MAX_SIZE) {
		bg->fwl_reset_addr = true;
		*written = empty_bytes;
		return FW_UPLOAD_ERR_NONE;
	}

	if (bg->fwl_reset_addr) {
		ret = cc1352_bootloader_download(bg, size, offset);
		if (ret < 0)
			return dev_err_probe(&bg->sd->dev,
					     FW_UPLOAD_ERR_HW_ERROR,
					     "Failed to send download cmd");

		bg->fwl_reset_addr = false;
	}

	ret = cc1352_bootloader_send_data(bg, data + offset, size);
	if (ret < 0)
		return dev_err_probe(&bg->sd->dev, FW_UPLOAD_ERR_HW_ERROR,
				     "Failed to flash firmware");
	*written = ret;

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err cc1352_poll_complete(struct fw_upload *fw_upload)
{
	u32 curr_crc32;
	struct gb_beagleplay *bg = fw_upload->dd_handle;

	if (cc1352_bootloader_crc32(bg, &curr_crc32) < 0)
		return dev_err_probe(&bg->sd->dev, FW_UPLOAD_ERR_HW_ERROR,
				     "Failed to fetch crc32");

	if (bg->fwl_crc32 != curr_crc32)
		return dev_err_probe(&bg->sd->dev, FW_UPLOAD_ERR_FW_INVALID,
				     "Invalid CRC32");

	if (cc1352_bootloader_reset(bg) < 0)
		return dev_err_probe(&bg->sd->dev, FW_UPLOAD_ERR_HW_ERROR,
				     "Failed to reset");

	dev_info(&bg->sd->dev, "CC1352 Flashing Successful");
	WRITE_ONCE(bg->flashing_mode, false);
	msleep(200);

	if (gb_greybus_init(bg) < 0)
		return dev_err_probe(&bg->sd->dev, FW_UPLOAD_ERR_RW_ERROR,
				     "Failed to initialize greybus");

	gb_beagleplay_start_svc(bg);

	return FW_UPLOAD_ERR_NONE;
}

static void cc1352_cancel(struct fw_upload *fw_upload)
{
	struct gb_beagleplay *bg = fw_upload->dd_handle;

	dev_info(&bg->sd->dev, "CC1352 Bootloader Cancel");

	cc1352_bootloader_reset(bg);
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

static const struct fw_upload_ops cc1352_bootloader_ops = {
	.prepare = cc1352_prepare,
	.write = cc1352_write,
	.poll_complete = cc1352_poll_complete,
	.cancel = cc1352_cancel,
	.cleanup = cc1352_cleanup
};

static int gb_fw_init(struct gb_beagleplay *bg)
{
	int ret;
	struct fw_upload *fwl;
	struct gpio_desc *desc;

	bg->fwl = NULL;
	bg->bootloader_backdoor_gpio = NULL;
	bg->rst_gpio = NULL;
	bg->flashing_mode = false;
	bg->fwl_cmd_response = 0;
	bg->fwl_ack = 0;
	init_completion(&bg->fwl_ack_com);
	init_completion(&bg->fwl_cmd_response_com);

	desc = devm_gpiod_get(&bg->sd->dev, "bootloader-backdoor", GPIOD_IN);
	if (IS_ERR(desc))
		return PTR_ERR(desc);
	bg->bootloader_backdoor_gpio = desc;

	desc = devm_gpiod_get(&bg->sd->dev, "reset", GPIOD_IN);
	if (IS_ERR(desc)) {
		ret = PTR_ERR(desc);
		goto free_boot;
	}
	bg->rst_gpio = desc;

	fwl = firmware_upload_register(THIS_MODULE, &bg->sd->dev, "cc1352p7",
				       &cc1352_bootloader_ops, bg);
	if (IS_ERR(fwl)) {
		ret = PTR_ERR(fwl);
		goto free_reset;
	}
	bg->fwl = fwl;

	return 0;

free_reset:
	devm_gpiod_put(&bg->sd->dev, bg->rst_gpio);
	bg->rst_gpio = NULL;
free_boot:
	devm_gpiod_put(&bg->sd->dev, bg->bootloader_backdoor_gpio);
	bg->bootloader_backdoor_gpio = NULL;
	return ret;
}

static void gb_fw_deinit(struct gb_beagleplay *bg)
{
	firmware_upload_unregister(bg->fwl);
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

	ret = gb_fw_init(bg);
	if (ret)
		goto free_hdlc;

	ret = gb_greybus_init(bg);
	if (ret)
		goto free_fw;

	gb_beagleplay_start_svc(bg);

	return 0;

free_fw:
	gb_fw_deinit(bg);
free_hdlc:
	hdlc_deinit(bg);
free_serdev:
	gb_serdev_deinit(bg);
	return ret;
}

static void gb_beagleplay_remove(struct serdev_device *serdev)
{
	struct gb_beagleplay *bg = serdev_device_get_drvdata(serdev);

	gb_fw_deinit(bg);
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
