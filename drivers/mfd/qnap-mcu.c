// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core driver for the microcontroller unit in QNAP NAS devices that is
 * connected via a dedicated UART port.
 *
 * Copyright (C) 2024 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/cleanup.h>
#include <linux/export.h>
#include <linux/mfd/core.h>
#include <linux/mfd/qnap-mcu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/serdev.h>
#include <linux/slab.h>

/* The longest command found so far is 5 bytes long */
#define QNAP_MCU_MAX_CMD_SIZE		5
#define QNAP_MCU_MAX_DATA_SIZE		36
#define QNAP_MCU_CHECKSUM_SIZE		1

#define QNAP_MCU_RX_BUFFER_SIZE		\
		(QNAP_MCU_MAX_DATA_SIZE + QNAP_MCU_CHECKSUM_SIZE)

#define QNAP_MCU_TX_BUFFER_SIZE		\
		(QNAP_MCU_MAX_CMD_SIZE + QNAP_MCU_CHECKSUM_SIZE)

#define QNAP_MCU_ACK_LEN		2
#define QNAP_MCU_VERSION_LEN		4

#define QNAP_MCU_TIMEOUT_MS		500

/**
 * struct qnap_mcu_reply - Reply to a command
 *
 * @data:	Buffer to store reply payload in
 * @length:	Expected reply length, including the checksum
 * @received:	Received number of bytes, so far
 * @done:	Triggered when the entire reply has been received
 */
struct qnap_mcu_reply {
	u8 *data;
	size_t length;
	size_t received;
	struct completion done;
};

/**
 * struct qnap_mcu - QNAP NAS embedded controller
 *
 * @serdev:	Pointer to underlying serdev
 * @bus_lock:	Lock to serialize access to the device
 * @reply:	Reply data structure
 * @variant:	Device variant specific information
 * @version:	MCU firmware version
 */
struct qnap_mcu {
	struct serdev_device *serdev;
	struct mutex bus_lock;
	struct qnap_mcu_reply reply;
	const struct qnap_mcu_variant *variant;
	u8 version[QNAP_MCU_VERSION_LEN];
};

/*
 * The QNAP-MCU uses a basic XOR checksum.
 * It is always the last byte and XORs the whole previous message.
 */
static u8 qnap_mcu_csum(const u8 *buf, size_t size)
{
	u8 csum = 0;

	while (size--)
		csum ^= *buf++;

	return csum;
}

static int qnap_mcu_write(struct qnap_mcu *mcu, const u8 *data, u8 data_size)
{
	unsigned char tx[QNAP_MCU_TX_BUFFER_SIZE];
	size_t length = data_size + QNAP_MCU_CHECKSUM_SIZE;

	if (length > sizeof(tx)) {
		dev_err(&mcu->serdev->dev, "data too big for transmit buffer");
		return -EINVAL;
	}

	memcpy(tx, data, data_size);
	tx[data_size] = qnap_mcu_csum(data, data_size);

	serdev_device_write_flush(mcu->serdev);

	return serdev_device_write(mcu->serdev, tx, length, HZ);
}

static size_t qnap_mcu_receive_buf(struct serdev_device *serdev, const u8 *buf, size_t size)
{
	struct device *dev = &serdev->dev;
	struct qnap_mcu *mcu = dev_get_drvdata(dev);
	struct qnap_mcu_reply *reply = &mcu->reply;
	const u8 *src = buf;
	const u8 *end = buf + size;

	if (!reply->length) {
		dev_warn(dev, "Received %zu bytes, we were not waiting for\n", size);
		return size;
	}

	while (src < end) {
		reply->data[reply->received] = *src++;
		reply->received++;

		if (reply->received == reply->length) {
			/* We don't expect any characters from the device now */
			reply->length = 0;

			complete(&reply->done);

			/*
			 * We report the consumed number of bytes. If there
			 * are still bytes remaining (though there shouldn't)
			 * the serdev layer will re-execute this handler with
			 * the remainder of the Rx bytes.
			 */
			return src - buf;
		}
	}

	/*
	 * The only way to get out of the above loop and end up here
	 * is through consuming all of the supplied data, so here we
	 * report that we processed it all.
	 */
	return size;
}

static const struct serdev_device_ops qnap_mcu_serdev_device_ops = {
	.receive_buf  = qnap_mcu_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

int qnap_mcu_exec(struct qnap_mcu *mcu,
		  const u8 *cmd_data, size_t cmd_data_size,
		  u8 *reply_data, size_t reply_data_size)
{
	unsigned char rx[QNAP_MCU_RX_BUFFER_SIZE];
	size_t length = reply_data_size + QNAP_MCU_CHECKSUM_SIZE;
	struct qnap_mcu_reply *reply = &mcu->reply;
	int ret = 0;

	if (length > sizeof(rx)) {
		dev_err(&mcu->serdev->dev, "expected data too big for receive buffer");
		return -EINVAL;
	}

	mutex_lock(&mcu->bus_lock);

	reply->data = rx;
	reply->length = length;
	reply->received = 0;
	reinit_completion(&reply->done);

	qnap_mcu_write(mcu, cmd_data, cmd_data_size);

	serdev_device_wait_until_sent(mcu->serdev, msecs_to_jiffies(QNAP_MCU_TIMEOUT_MS));

	if (!wait_for_completion_timeout(&reply->done, msecs_to_jiffies(QNAP_MCU_TIMEOUT_MS))) {
		dev_err(&mcu->serdev->dev, "Command timeout\n");
		ret = -ETIMEDOUT;
	} else {
		u8 crc = qnap_mcu_csum(rx, reply_data_size);

		if (crc != rx[reply_data_size]) {
			dev_err(&mcu->serdev->dev,
				"Invalid Checksum received\n");
			ret = -EIO;
		} else {
			memcpy(reply_data, rx, reply_data_size);
		}
	}

	mutex_unlock(&mcu->bus_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(qnap_mcu_exec);

int qnap_mcu_exec_with_ack(struct qnap_mcu *mcu,
			   const u8 *cmd_data, size_t cmd_data_size)
{
	u8 ack[QNAP_MCU_ACK_LEN];
	int ret;

	ret = qnap_mcu_exec(mcu, cmd_data, cmd_data_size, ack, sizeof(ack));
	if (ret)
		return ret;

	/* Should return @0 */
	if (ack[0] != '@' || ack[1] != '0') {
		dev_err(&mcu->serdev->dev, "Did not receive ack\n");
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qnap_mcu_exec_with_ack);

static int qnap_mcu_get_version(struct qnap_mcu *mcu)
{
	const u8 cmd[] = { '%', 'V' };
	u8 rx[14];
	int ret;

	/* Reply is the 2 command-bytes + 4 bytes describing the version */
	ret = qnap_mcu_exec(mcu, cmd, sizeof(cmd), rx, QNAP_MCU_VERSION_LEN + 2);
	if (ret)
		return ret;

	memcpy(mcu->version, &rx[2], QNAP_MCU_VERSION_LEN);

	return 0;
}

/*
 * The MCU controls power to the peripherals but not the CPU.
 *
 * So using the PMIC to power off the system keeps the MCU and hard-drives
 * running. This also then prevents the system from turning back on until
 * the MCU is turned off by unplugging the power cable.
 * Turning off the MCU alone on the other hand turns off the hard drives,
 * LEDs, etc while the main SoC stays running - including its network ports.
 */
static int qnap_mcu_power_off(struct sys_off_data *data)
{
	const u8 cmd[] = { '@', 'C', '0' };
	struct qnap_mcu *mcu = data->cb_data;
	int ret;

	ret = qnap_mcu_exec_with_ack(mcu, cmd, sizeof(cmd));
	if (ret) {
		dev_err(&mcu->serdev->dev, "MCU poweroff failed %d\n", ret);
		return NOTIFY_STOP;
	}

	return NOTIFY_DONE;
}

static const struct qnap_mcu_variant qnap_ts433_mcu = {
	.baud_rate = 115200,
	.num_drives = 4,
	.fan_pwm_min = 51,  /* Specified in original model.conf */
	.fan_pwm_max = 255,
	.usb_led = true,
};

static struct mfd_cell qnap_mcu_cells[] = {
	{ .name = "qnap-mcu-input", },
	{ .name = "qnap-mcu-leds", },
	{ .name = "qnap-mcu-hwmon", }
};

static int qnap_mcu_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct qnap_mcu *mcu;
	int ret;

	mcu = devm_kzalloc(dev, sizeof(*mcu), GFP_KERNEL);
	if (!mcu)
		return -ENOMEM;

	mcu->serdev = serdev;
	dev_set_drvdata(dev, mcu);

	mcu->variant = of_device_get_match_data(dev);
	if (!mcu->variant)
		return -ENODEV;

	mutex_init(&mcu->bus_lock);
	init_completion(&mcu->reply.done);

	serdev_device_set_client_ops(serdev, &qnap_mcu_serdev_device_ops);
	ret = devm_serdev_device_open(dev, serdev);
	if (ret)
		return ret;

	serdev_device_set_baudrate(serdev, mcu->variant->baud_rate);
	serdev_device_set_flow_control(serdev, false);

	ret = serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set parity\n");

	ret = qnap_mcu_get_version(mcu);
	if (ret)
		return ret;

	ret = devm_register_sys_off_handler(dev,
					    SYS_OFF_MODE_POWER_OFF_PREPARE,
					    SYS_OFF_PRIO_DEFAULT,
					    &qnap_mcu_power_off, mcu);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register poweroff handler\n");

	for (int i = 0; i < ARRAY_SIZE(qnap_mcu_cells); i++) {
		qnap_mcu_cells[i].platform_data = mcu->variant;
		qnap_mcu_cells[i].pdata_size = sizeof(*mcu->variant);
	}

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, qnap_mcu_cells,
				   ARRAY_SIZE(qnap_mcu_cells), NULL, 0, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add child devices\n");

	return 0;
}

static const struct of_device_id qnap_mcu_dt_ids[] = {
	{ .compatible = "qnap,ts433-mcu", .data = &qnap_ts433_mcu },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, qnap_mcu_dt_ids);

static struct serdev_device_driver qnap_mcu_drv = {
	.probe = qnap_mcu_probe,
	.driver = {
		.name = "qnap-mcu",
		.of_match_table = qnap_mcu_dt_ids,
	},
};
module_serdev_device_driver(qnap_mcu_drv);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("QNAP MCU core driver");
MODULE_LICENSE("GPL");
