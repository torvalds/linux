// SPDX-License-Identifier: GPL-2.0-only
/*
 * UART interface for ChromeOS Embedded Controller
 *
 * Copyright 2020-2022 Google LLC.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <uapi/linux/sched/types.h>

#include "cros_ec.h"

/*
 * EC sends contiguous bytes of response packet on UART AP RX.
 * TTY driver in AP accumulates incoming bytes and calls the registered callback
 * function. Byte count can range from 1 to MAX bytes supported by EC.
 * This driver should wait for long time for all callbacks to be processed.
 * Considering the worst case scenario, wait for 500 msec. This timeout should
 * account for max latency and some additional guard time.
 * Best case: Entire packet is received in ~200 ms, wait queue will be released
 * and packet will be processed.
 * Worst case: TTY driver sends bytes in multiple callbacks. In this case this
 * driver will wait for ~1 sec beyond which it will timeout.
 * This timeout value should not exceed ~500 msec because in case if
 * EC_CMD_REBOOT_EC sent, high level driver should be able to intercept EC
 * in RO.
 */
#define EC_MSG_DEADLINE_MS		500

/**
 * struct response_info - Encapsulate EC response related
 *			information for passing between function
 *			cros_ec_uart_pkt_xfer() and cros_ec_uart_rx_bytes()
 *			callback.
 * @data:		Copy the data received from EC here.
 * @max_size:		Max size allocated for the @data buffer. If the
 *			received data exceeds this value, we log an error.
 * @size:		Actual size of data received from EC. This is also
 *			used to accumulate byte count with response is received
 *			in dma chunks.
 * @exp_len:		Expected bytes of response from EC including header.
 * @status:		Re-init to 0 before sending a cmd. Updated to 1 when
 *			a response is successfully received, or an error number
 *			on failure.
 * @wait_queue:	Wait queue EC response where the cros_ec sends request
 *			to EC and waits
 */
struct response_info {
	void *data;
	size_t max_size;
	size_t size;
	size_t exp_len;
	int status;
	wait_queue_head_t wait_queue;
};

/**
 * struct cros_ec_uart - information about a uart-connected EC
 *
 * @serdev:		serdev uart device we are connected to.
 * @baudrate:		UART baudrate of attached EC device.
 * @flowcontrol:	UART flowcontrol of attached device.
 * @irq:		Linux IRQ number of associated serial device.
 * @response:		Response info passing between cros_ec_uart_pkt_xfer()
 *			and cros_ec_uart_rx_bytes()
 */
struct cros_ec_uart {
	struct serdev_device *serdev;
	u32 baudrate;
	u8 flowcontrol;
	u32 irq;
	struct response_info response;
};

static size_t cros_ec_uart_rx_bytes(struct serdev_device *serdev,
				    const u8 *data, size_t count)
{
	struct ec_host_response *host_response;
	struct cros_ec_device *ec_dev = serdev_device_get_drvdata(serdev);
	struct cros_ec_uart *ec_uart = ec_dev->priv;
	struct response_info *resp = &ec_uart->response;

	/* Check if bytes were sent out of band */
	if (!resp->data) {
		/* Discard all bytes */
		dev_warn(ec_dev->dev, "Bytes received out of band, dropping them.\n");
		return count;
	}

	/*
	 * Check if incoming bytes + resp->size is greater than allocated
	 * buffer in din by cros_ec. This will ensure that if EC sends more
	 * bytes than max_size, waiting process will be notified with an error.
	 */
	if (resp->size + count > resp->max_size) {
		resp->status = -EMSGSIZE;
		wake_up(&resp->wait_queue);
		return count;
	}

	memcpy(resp->data + resp->size, data, count);

	resp->size += count;

	/* Read data_len if we received response header and if exp_len was not read before. */
	if (resp->size >= sizeof(*host_response) && resp->exp_len == 0) {
		host_response = (struct ec_host_response *)resp->data;
		resp->exp_len = host_response->data_len + sizeof(*host_response);
	}

	/* If driver received response header and payload from EC, wake up the wait queue. */
	if (resp->size >= sizeof(*host_response) && resp->size == resp->exp_len) {
		resp->status = 1;
		wake_up(&resp->wait_queue);
	}

	return count;
}

static int cros_ec_uart_pkt_xfer(struct cros_ec_device *ec_dev,
				 struct cros_ec_command *ec_msg)
{
	struct cros_ec_uart *ec_uart = ec_dev->priv;
	struct serdev_device *serdev = ec_uart->serdev;
	struct response_info *resp = &ec_uart->response;
	struct ec_host_response *host_response;
	unsigned int len;
	int ret, i;
	u8 sum;

	len = cros_ec_prepare_tx(ec_dev, ec_msg);
	dev_dbg(ec_dev->dev, "Prepared len=%d\n", len);

	/* Setup for incoming response */
	resp->data = ec_dev->din;
	resp->max_size = ec_dev->din_size;
	resp->size = 0;
	resp->exp_len = 0;
	resp->status = 0;

	ret = serdev_device_write_buf(serdev, ec_dev->dout, len);
	if (ret < 0 || ret < len) {
		dev_err(ec_dev->dev, "Unable to write data\n");
		if (ret >= 0)
			ret = -EIO;
		goto exit;
	}

	ret = wait_event_timeout(resp->wait_queue, resp->status,
				 msecs_to_jiffies(EC_MSG_DEADLINE_MS));
	if (ret == 0) {
		dev_warn(ec_dev->dev, "Timed out waiting for response.\n");
		ret = -ETIMEDOUT;
		goto exit;
	}

	if (resp->status < 0) {
		ret = resp->status;
		dev_warn(ec_dev->dev, "Error response received: %d\n", ret);
		goto exit;
	}

	host_response = (struct ec_host_response *)ec_dev->din;
	ec_msg->result = host_response->result;

	if (host_response->data_len > ec_msg->insize) {
		dev_err(ec_dev->dev, "Resp too long (%d bytes, expected %d)\n",
			host_response->data_len, ec_msg->insize);
		ret = -ENOSPC;
		goto exit;
	}

	/* Validate checksum */
	sum = 0;
	for (i = 0; i < sizeof(*host_response) + host_response->data_len; i++)
		sum += ec_dev->din[i];

	if (sum) {
		dev_err(ec_dev->dev, "Bad packet checksum calculated %x\n", sum);
		ret = -EBADMSG;
		goto exit;
	}

	memcpy(ec_msg->data, ec_dev->din + sizeof(*host_response), host_response->data_len);

	ret = host_response->data_len;

exit:
	/* Invalidate response buffer to guard against out of band rx data */
	resp->data = NULL;

	if (ec_msg->command == EC_CMD_REBOOT_EC)
		msleep(EC_REBOOT_DELAY_MS);

	return ret;
}

static int cros_ec_uart_resource(struct acpi_resource *ares, void *data)
{
	struct cros_ec_uart *ec_uart = data;
	struct acpi_resource_uart_serialbus *sb = &ares->data.uart_serial_bus;

	if (ares->type == ACPI_RESOURCE_TYPE_SERIAL_BUS &&
	    sb->type == ACPI_RESOURCE_SERIAL_TYPE_UART) {
		ec_uart->baudrate = sb->default_baud_rate;
		dev_dbg(&ec_uart->serdev->dev, "Baudrate %d\n", ec_uart->baudrate);

		ec_uart->flowcontrol = sb->flow_control;
		dev_dbg(&ec_uart->serdev->dev, "Flow control %d\n", ec_uart->flowcontrol);
	}

	return 0;
}

static int cros_ec_uart_acpi_probe(struct cros_ec_uart *ec_uart)
{
	int ret;
	LIST_HEAD(resources);
	struct acpi_device *adev = ACPI_COMPANION(&ec_uart->serdev->dev);

	ret = acpi_dev_get_resources(adev, &resources, cros_ec_uart_resource, ec_uart);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&resources);

	/* Retrieve GpioInt and translate it to Linux IRQ number */
	ret = acpi_dev_gpio_irq_get(adev, 0);
	if (ret < 0)
		return ret;

	ec_uart->irq = ret;
	dev_dbg(&ec_uart->serdev->dev, "IRQ number %d\n", ec_uart->irq);

	return 0;
}

static const struct serdev_device_ops cros_ec_uart_client_ops = {
	.receive_buf = cros_ec_uart_rx_bytes,
};

static int cros_ec_uart_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct cros_ec_device *ec_dev;
	struct cros_ec_uart *ec_uart;
	int ret;

	ec_uart = devm_kzalloc(dev, sizeof(*ec_uart), GFP_KERNEL);
	if (!ec_uart)
		return -ENOMEM;

	ec_dev = cros_ec_device_alloc(dev);
	if (!ec_dev)
		return -ENOMEM;

	serdev_device_set_drvdata(serdev, ec_dev);
	init_waitqueue_head(&ec_uart->response.wait_queue);

	ec_uart->serdev = serdev;

	ret = cros_ec_uart_acpi_probe(ec_uart);
	if (ret < 0) {
		dev_err(dev, "Failed to get ACPI info (%d)", ret);
		return ret;
	}

	/* Initialize ec_dev for cros_ec  */
	ec_dev->phys_name = dev_name(dev);
	ec_dev->priv = ec_uart;
	ec_dev->irq = ec_uart->irq;
	ec_dev->cmd_xfer = NULL;
	ec_dev->pkt_xfer = cros_ec_uart_pkt_xfer;

	serdev_device_set_client_ops(serdev, &cros_ec_uart_client_ops);

	ret = devm_serdev_device_open(dev, serdev);
	if (ret) {
		dev_err(dev, "Unable to open UART device");
		return ret;
	}

	ret = serdev_device_set_baudrate(serdev, ec_uart->baudrate);
	if (ret < 0) {
		dev_err(dev, "Failed to set up host baud rate (%d)", ret);
		return ret;
	}

	serdev_device_set_flow_control(serdev, ec_uart->flowcontrol);

	return cros_ec_register(ec_dev);
}

static void cros_ec_uart_remove(struct serdev_device *serdev)
{
	struct cros_ec_device *ec_dev = serdev_device_get_drvdata(serdev);

	cros_ec_unregister(ec_dev);
};

static int __maybe_unused cros_ec_uart_suspend(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_suspend(ec_dev);
}

static int __maybe_unused cros_ec_uart_resume(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_resume(ec_dev);
}

static SIMPLE_DEV_PM_OPS(cros_ec_uart_pm_ops, cros_ec_uart_suspend,
			 cros_ec_uart_resume);

static const struct of_device_id cros_ec_uart_of_match[] = {
	{ .compatible = "google,cros-ec-uart" },
	{}
};
MODULE_DEVICE_TABLE(of, cros_ec_uart_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id cros_ec_uart_acpi_id[] = {
	{ "GOOG0019", 0 },
	{}
};

MODULE_DEVICE_TABLE(acpi, cros_ec_uart_acpi_id);
#endif

static struct serdev_device_driver cros_ec_uart_driver = {
	.driver	= {
		.name	= "cros-ec-uart",
		.acpi_match_table = ACPI_PTR(cros_ec_uart_acpi_id),
		.of_match_table = cros_ec_uart_of_match,
		.pm	= &cros_ec_uart_pm_ops,
	},
	.probe		= cros_ec_uart_probe,
	.remove		= cros_ec_uart_remove,
};

module_serdev_device_driver(cros_ec_uart_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UART interface for ChromeOS Embedded Controller");
MODULE_AUTHOR("Bhanu Prakash Maiya <bhanumaiya@chromium.org>");
