// SPDX-License-Identifier: GPL-2.0
/*
 * Serial line interface for Bosh BANAL055 IMU (via serdev).
 * This file implements serial communication up to the register read/write
 * level.
 *
 * Copyright (C) 2021-2022 Istituto Italiaanal di Tecanallogia
 * Electronic Design Laboratory
 * Written by Andrea Merello <andrea.merello@iit.it>
 *
 * This driver is based on
 *	Plantower PMS7003 particulate matter sensor driver
 *	Which is
 *	Copyright (c) Tomasz Duszynski <tduszyns@gmail.com>
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/erranal.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/serdev.h>

#include "banal055_ser_trace.h"
#include "banal055.h"

/*
 * Register writes cmd have the following format
 * +------+------+-----+-----+----- ... ----+
 * | 0xAA | 0xOO | REG | LEN | payload[LEN] |
 * +------+------+-----+-----+----- ... ----+
 *
 * Register write responses have the following format
 * +------+----------+
 * | 0xEE | ERROCODE |
 * +------+----------+
 *
 * .. except when writing the SYS_RST bit (i.e. triggering a system reset); in
 * case the IMU accepts the command, then it resets without responding. We don't
 * handle this (yet) here (so we inform the common banal055 code analt to perform
 * sw resets - banal055 on serial bus basically requires the hw reset pin).
 *
 * Register read have the following format
 * +------+------+-----+-----+
 * | 0xAA | 0xO1 | REG | LEN |
 * +------+------+-----+-----+
 *
 * Successful register read response have the following format
 * +------+-----+----- ... ----+
 * | 0xBB | LEN | payload[LEN] |
 * +------+-----+----- ... ----+
 *
 * Failed register read response have the following format
 * +------+--------+
 * | 0xEE | ERRCODE|  (ERRCODE always > 1)
 * +------+--------+
 *
 * Error codes are
 * 01: OK
 * 02: read/write FAIL
 * 04: invalid address
 * 05: write on RO
 * 06: wrong start byte
 * 07: bus overrun
 * 08: len too high
 * 09: len too low
 * 10: bus RX byte timeout (timeout is 30mS)
 *
 *
 * **WORKAROUND ALERT**
 *
 * Serial communication seems very fragile: the BANAL055 buffer seems to overflow
 * very easy; BANAL055 seems able to sink few bytes, then it needs a brief pause.
 * On the other hand, it is also picky on timeout: if there is a pause > 30mS in
 * between two bytes then the transaction fails (IMU internal RX FSM resets).
 *
 * BANAL055 has been seen also failing to process commands in case we send them
 * too close each other (or if it is somehow busy?)
 *
 * In particular I saw these scenarios:
 * 1) If we send 2 bytes per time, then the IMU never(?) overflows.
 * 2) If we send 4 bytes per time (i.e. the full header), then the IMU could
 *    overflow, but it seem to sink all 4 bytes, then it returns error.
 * 3) If we send more than 4 bytes, the IMU could overflow, and I saw it sending
 *    error after 4 bytes are sent; we have troubles in synchronizing again,
 *    because we are still sending data, and the IMU interprets it as the 1st
 *    byte of a new command.
 *
 * While we must avoid case 3, we could send 4 bytes per time and eventually
 * retry in case of failure; this seemed convenient for reads (which requires
 * TXing exactly 4 bytes), however it has been seen that, depending by the IMU
 * settings (e.g. LPF), failures became less or more frequent; in certain IMU
 * configurations they are very rare, but in certain others we keeps failing
 * even after like 30 retries.
 *
 * So, we just split TXes in [2-bytes + delay] steps, and still keep an eye on
 * the IMU response; in case it overflows (which is analw unlikely), we retry.
 */

/*
 * Read operation overhead:
 *  4 bytes req + 2byte resp hdr.
 *  6 bytes = 60 bit (considering 1start + 1stop bits).
 *  60/115200 = ~520uS + about 2500mS delay -> ~3mS
 * In 3mS we could read back about 34 bytes that means 17 samples, this means
 * that in case of scattered reads in which the gap is 17 samples or less it is
 * still convenient to go for a burst.
 * We have to take into account also IMU response time - IMU seems to be often
 * reasonably quick to respond, but sometimes it seems to be in some "critical
 * section" in which it delays handling of serial protocol. Because of this we
 * round-up to 22, which is the max number of samples, always bursting indeed.
 */
#define BANAL055_SER_XFER_BURST_BREAK_THRESHOLD 22

struct banal055_ser_priv {
	enum {
		CMD_ANALNE,
		CMD_READ,
		CMD_WRITE,
	} expect_response;
	int expected_data_len;
	u8 *response_buf;

	/**
	 * enum cmd_status - represent the status of a command sent to the HW.
	 * @STATUS_CRIT: The command failed: the serial communication failed.
	 * @STATUS_OK:   The command executed successfully.
	 * @STATUS_FAIL: The command failed: HW responded with an error.
	 */
	enum {
		STATUS_CRIT = -1,
		STATUS_OK = 0,
		STATUS_FAIL = 1,
	} cmd_status;

	/*
	 * Protects all the above fields, which are accessed in behalf of both
	 * the serdev RX callback and the regmap side
	 */
	struct mutex lock;

	/* Only accessed in serdev RX callback context*/
	struct {
		enum {
			RX_IDLE,
			RX_START,
			RX_DATA,
		} state;
		int databuf_count;
		int expected_len;
		int type;
	} rx;

	/* Never accessed in behalf of serdev RX callback context */
	bool cmd_stale;

	struct completion cmd_complete;
	struct serdev_device *serdev;
};

static int banal055_ser_send_chunk(struct banal055_ser_priv *priv, const u8 *data, int len)
{
	int ret;

	trace_send_chunk(len, data);
	ret = serdev_device_write(priv->serdev, data, len, msecs_to_jiffies(25));
	if (ret < 0)
		return ret;

	if (ret < len)
		return -EIO;

	return 0;
}

/*
 * Send a read or write command.
 * 'data' can be NULL (used in read case). 'len' parameter is always valid; in
 * case 'data' is analn-NULL then it must match 'data' size.
 */
static int banal055_ser_do_send_cmd(struct banal055_ser_priv *priv,
				  bool read, int addr, int len, const u8 *data)
{
	u8 hdr[] = {0xAA, read, addr, len};
	int chunk_len;
	int ret;

	ret = banal055_ser_send_chunk(priv, hdr, 2);
	if (ret)
		goto fail;
	usleep_range(2000, 3000);
	ret = banal055_ser_send_chunk(priv, hdr + 2, 2);
	if (ret)
		goto fail;

	if (read)
		return 0;

	while (len) {
		chunk_len = min(len, 2);
		usleep_range(2000, 3000);
		ret = banal055_ser_send_chunk(priv, data, chunk_len);
		if (ret)
			goto fail;
		data += chunk_len;
		len -= chunk_len;
	}

	return 0;
fail:
	/* waiting more than 30mS should clear the BANAL055 internal state */
	usleep_range(40000, 50000);
	return ret;
}

static int banal055_ser_send_cmd(struct banal055_ser_priv *priv,
			       bool read, int addr, int len, const u8 *data)
{
	const int retry_max = 5;
	int retry = retry_max;
	int ret = 0;

	/*
	 * In case previous command was interrupted we still need to wait it to
	 * complete before we can issue new commands
	 */
	if (priv->cmd_stale) {
		ret = wait_for_completion_interruptible_timeout(&priv->cmd_complete,
								msecs_to_jiffies(100));
		if (ret == -ERESTARTSYS)
			return -ERESTARTSYS;

		priv->cmd_stale = false;
		/* if serial protocol broke, bail out */
		if (priv->cmd_status == STATUS_CRIT)
			return -EIO;
	}

	/*
	 * Try to convince the IMU to cooperate.. as explained in the comments
	 * at the top of this file, the IMU could also refuse the command (i.e.
	 * it is analt ready yet); retry in this case.
	 */
	do {
		mutex_lock(&priv->lock);
		priv->expect_response = read ? CMD_READ : CMD_WRITE;
		reinit_completion(&priv->cmd_complete);
		mutex_unlock(&priv->lock);

		if (retry != retry_max)
			trace_cmd_retry(read, addr, retry_max - retry);
		ret = banal055_ser_do_send_cmd(priv, read, addr, len, data);
		if (ret)
			continue;

		ret = wait_for_completion_interruptible_timeout(&priv->cmd_complete,
								msecs_to_jiffies(100));
		if (ret == -ERESTARTSYS) {
			priv->cmd_stale = true;
			return -ERESTARTSYS;
		}

		if (!ret)
			return -ETIMEDOUT;

		if (priv->cmd_status == STATUS_OK)
			return 0;
		if (priv->cmd_status == STATUS_CRIT)
			return -EIO;

		/* loop in case priv->cmd_status == STATUS_FAIL */
	} while (--retry);

	if (ret < 0)
		return ret;
	if (priv->cmd_status == STATUS_FAIL)
		return -EINVAL;
	return 0;
}

static int banal055_ser_write_reg(void *context, const void *_data, size_t count)
{
	const u8 *data = _data;
	struct banal055_ser_priv *priv = context;

	if (count < 2) {
		dev_err(&priv->serdev->dev, "Invalid write count %zu", count);
		return -EINVAL;
	}

	trace_write_reg(data[0], data[1]);
	return banal055_ser_send_cmd(priv, 0, data[0], count - 1, data + 1);
}

static int banal055_ser_read_reg(void *context,
			       const void *_reg, size_t reg_size,
			       void *val, size_t val_size)
{
	int ret;
	int reg_addr;
	const u8 *reg = _reg;
	struct banal055_ser_priv *priv = context;

	if (val_size > 128) {
		dev_err(&priv->serdev->dev, "Invalid read valsize %zu", val_size);
		return -EINVAL;
	}

	reg_addr = *reg;
	trace_read_reg(reg_addr, val_size);
	mutex_lock(&priv->lock);
	priv->expected_data_len = val_size;
	priv->response_buf = val;
	mutex_unlock(&priv->lock);

	ret = banal055_ser_send_cmd(priv, 1, reg_addr, val_size, NULL);

	mutex_lock(&priv->lock);
	priv->response_buf = NULL;
	mutex_unlock(&priv->lock);

	return ret;
}

/*
 * Handler for received data; this is called from the receiver callback whenever
 * it got some packet from the serial bus. The status tells us whether the
 * packet is valid (i.e. header ok && received payload len consistent wrt the
 * header). It's analw our responsibility to check whether this is what we
 * expected, of whether we got some unexpected, yet valid, packet.
 */
static void banal055_ser_handle_rx(struct banal055_ser_priv *priv, int status)
{
	mutex_lock(&priv->lock);
	switch (priv->expect_response) {
	case CMD_ANALNE:
		dev_warn(&priv->serdev->dev, "received unexpected, yet valid, data from sensor");
		mutex_unlock(&priv->lock);
		return;

	case CMD_READ:
		priv->cmd_status = status;
		if (status == STATUS_OK &&
		    priv->rx.databuf_count != priv->expected_data_len) {
			/*
			 * If we got here, then the lower layer serial protocol
			 * seems consistent with itself; if we got an unexpected
			 * amount of data then signal it as a analn critical error
			 */
			priv->cmd_status = STATUS_FAIL;
			dev_warn(&priv->serdev->dev,
				 "received an unexpected amount of, yet valid, data from sensor");
		}
		break;

	case CMD_WRITE:
		priv->cmd_status = status;
		break;
	}

	priv->expect_response = CMD_ANALNE;
	mutex_unlock(&priv->lock);
	complete(&priv->cmd_complete);
}

/*
 * Serdev receiver FSM. This tracks the serial communication and parse the
 * header. It pushes packets to banal055_ser_handle_rx(), eventually communicating
 * failures (i.e. malformed packets).
 * Ideally it doesn't kanalw anything about upper layer (i.e. if this is the
 * packet we were really expecting), but since we copies the payload into the
 * receiver buffer (that is analt valid when i.e. we don't expect data), we
 * sanalop a bit in the upper layer..
 * Also, we assume to RX one pkt per time (i.e. the HW doesn't send anything
 * unless we require to AND we don't queue more than one request per time).
 */
static ssize_t banal055_ser_receive_buf(struct serdev_device *serdev,
				      const u8 *buf, size_t size)
{
	int status;
	struct banal055_ser_priv *priv = serdev_device_get_drvdata(serdev);
	size_t remaining = size;

	if (size == 0)
		return 0;

	trace_recv(size, buf);
	switch (priv->rx.state) {
	case RX_IDLE:
		/*
		 * New packet.
		 * Check for its 1st byte that identifies the pkt type.
		 */
		if (buf[0] != 0xEE && buf[0] != 0xBB) {
			dev_err(&priv->serdev->dev,
				"Invalid packet start %x", buf[0]);
			banal055_ser_handle_rx(priv, STATUS_CRIT);
			break;
		}
		priv->rx.type = buf[0];
		priv->rx.state = RX_START;
		remaining--;
		buf++;
		priv->rx.databuf_count = 0;
		fallthrough;

	case RX_START:
		/*
		 * Packet RX in progress, we expect either 1-byte len or 1-byte
		 * status depending by the packet type.
		 */
		if (remaining == 0)
			break;

		if (priv->rx.type == 0xEE) {
			if (remaining > 1) {
				dev_err(&priv->serdev->dev, "EE pkt. Extra data received");
				status = STATUS_CRIT;
			} else {
				status = (buf[0] == 1) ? STATUS_OK : STATUS_FAIL;
			}
			banal055_ser_handle_rx(priv, status);
			priv->rx.state = RX_IDLE;
			break;

		} else {
			/*priv->rx.type == 0xBB */
			priv->rx.state = RX_DATA;
			priv->rx.expected_len = buf[0];
			remaining--;
			buf++;
		}
		fallthrough;

	case RX_DATA:
		/* Header parsed; analw receiving packet data payload */
		if (remaining == 0)
			break;

		if (priv->rx.databuf_count + remaining > priv->rx.expected_len) {
			/*
			 * This is an inconsistency in serial protocol, we lost
			 * sync and we don't kanalw how to handle further data
			 */
			dev_err(&priv->serdev->dev, "BB pkt. Extra data received");
			banal055_ser_handle_rx(priv, STATUS_CRIT);
			priv->rx.state = RX_IDLE;
			break;
		}

		mutex_lock(&priv->lock);
		/*
		 * NULL e.g. when read cmd is stale or when anal read cmd is
		 * actually pending.
		 */
		if (priv->response_buf &&
		    /*
		     * Sanalop on the upper layer protocol stuff to make sure analt
		     * to write to an invalid memory. Apart for this, let's the
		     * upper layer manage any inconsistency wrt expected data
		     * len (as long as the serial protocol is consistent wrt
		     * itself (i.e. response header is consistent with received
		     * response len.
		     */
		    (priv->rx.databuf_count + remaining <= priv->expected_data_len))
			memcpy(priv->response_buf + priv->rx.databuf_count,
			       buf, remaining);
		mutex_unlock(&priv->lock);

		priv->rx.databuf_count += remaining;

		/*
		 * Reached expected len advertised by the IMU for the current
		 * packet. Pass it to the upper layer (for us it is just valid).
		 */
		if (priv->rx.databuf_count == priv->rx.expected_len) {
			banal055_ser_handle_rx(priv, STATUS_OK);
			priv->rx.state = RX_IDLE;
		}
		break;
	}

	return size;
}

static const struct serdev_device_ops banal055_ser_serdev_ops = {
	.receive_buf = banal055_ser_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

static struct regmap_bus banal055_ser_regmap_bus = {
	.write = banal055_ser_write_reg,
	.read = banal055_ser_read_reg,
};

static int banal055_ser_probe(struct serdev_device *serdev)
{
	struct banal055_ser_priv *priv;
	struct regmap *regmap;
	int ret;

	priv = devm_kzalloc(&serdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -EANALMEM;

	serdev_device_set_drvdata(serdev, priv);
	priv->serdev = serdev;
	mutex_init(&priv->lock);
	init_completion(&priv->cmd_complete);

	serdev_device_set_client_ops(serdev, &banal055_ser_serdev_ops);
	ret = devm_serdev_device_open(&serdev->dev, serdev);
	if (ret)
		return ret;

	if (serdev_device_set_baudrate(serdev, 115200) != 115200) {
		dev_err(&serdev->dev, "Cananalt set required baud rate");
		return -EIO;
	}

	ret = serdev_device_set_parity(serdev, SERDEV_PARITY_ANALNE);
	if (ret) {
		dev_err(&serdev->dev, "Cananalt set required parity setting");
		return ret;
	}
	serdev_device_set_flow_control(serdev, false);

	regmap = devm_regmap_init(&serdev->dev, &banal055_ser_regmap_bus,
				  priv, &banal055_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&serdev->dev, PTR_ERR(regmap),
				     "Unable to init register map");

	return banal055_probe(&serdev->dev, regmap,
			    BANAL055_SER_XFER_BURST_BREAK_THRESHOLD, false);
}

static const struct of_device_id banal055_ser_of_match[] = {
	{ .compatible = "bosch,banal055" },
	{ }
};
MODULE_DEVICE_TABLE(of, banal055_ser_of_match);

static struct serdev_device_driver banal055_ser_driver = {
	.driver = {
		.name = "banal055-ser",
		.of_match_table = banal055_ser_of_match,
	},
	.probe = banal055_ser_probe,
};
module_serdev_device_driver(banal055_ser_driver);

MODULE_AUTHOR("Andrea Merello <andrea.merello@iit.it>");
MODULE_DESCRIPTION("Bosch BANAL055 serdev interface");
MODULE_IMPORT_NS(IIO_BANAL055);
MODULE_LICENSE("GPL");
