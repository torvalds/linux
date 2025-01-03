// SPDX-License-Identifier: GPL-2.0
//
// QiHeng Electronics ch341a USB-to-SPI adapter driver
//
// Copyright (C) 2024 Johannes Thumshirn <jth@kernel.org>
//
// Based on ch341a_spi.c from the flashrom project.

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/spi/spi.h>

#define CH341_PACKET_LENGTH 32
#define CH341_DEFAULT_TIMEOUT 1000

#define CH341A_CMD_UIO_STREAM 0xab

#define CH341A_CMD_UIO_STM_END 0x20
#define CH341A_CMD_UIO_STM_DIR 0x40
#define CH341A_CMD_UIO_STM_OUT 0x80

#define CH341A_CMD_I2C_STREAM 0xaa
#define CH341A_CMD_I2C_STM_SET 0x60
#define CH341A_CMD_I2C_STM_END 0x00

#define CH341A_CMD_SPI_STREAM 0xa8

#define CH341A_STM_I2C_100K 0x01

struct ch341_spi_dev {
	struct spi_controller *ctrl;
	struct usb_device *udev;
	unsigned int write_pipe;
	unsigned int read_pipe;
	int rx_len;
	void *rx_buf;
	u8 *tx_buf;
	struct urb *rx_urb;
	struct spi_device *spidev;
};

static void ch341_set_cs(struct spi_device *spi, bool is_high)
{
	struct ch341_spi_dev *ch341 =
		spi_controller_get_devdata(spi->controller);
	int err;

	memset(ch341->tx_buf, 0, CH341_PACKET_LENGTH);
	ch341->tx_buf[0] = CH341A_CMD_UIO_STREAM;
	ch341->tx_buf[1] = CH341A_CMD_UIO_STM_OUT | (is_high ? 0x36 : 0x37);

	if (is_high) {
		ch341->tx_buf[2] = CH341A_CMD_UIO_STM_DIR | 0x3f;
		ch341->tx_buf[3] = CH341A_CMD_UIO_STM_END;
	} else {
		ch341->tx_buf[2] = CH341A_CMD_UIO_STM_END;
	}

	err = usb_bulk_msg(ch341->udev, ch341->write_pipe, ch341->tx_buf,
			   (is_high ? 4 : 3), NULL, CH341_DEFAULT_TIMEOUT);
	if (err)
		dev_err(&spi->dev,
			"error sending USB message for setting CS (%d)\n", err);
}

static int ch341_transfer_one(struct spi_controller *host,
			      struct spi_device *spi,
			      struct spi_transfer *trans)
{
	struct ch341_spi_dev *ch341 =
		spi_controller_get_devdata(spi->controller);
	int len;
	int ret;

	len = min(CH341_PACKET_LENGTH, trans->len + 1);

	memset(ch341->tx_buf, 0, CH341_PACKET_LENGTH);

	ch341->tx_buf[0] = CH341A_CMD_SPI_STREAM;

	memcpy(ch341->tx_buf + 1, trans->tx_buf, len);

	ret = usb_bulk_msg(ch341->udev, ch341->write_pipe, ch341->tx_buf, len,
			   NULL, CH341_DEFAULT_TIMEOUT);
	if (ret)
		return ret;

	return usb_bulk_msg(ch341->udev, ch341->read_pipe, trans->rx_buf,
			    len - 1, NULL, CH341_DEFAULT_TIMEOUT);
}

static void ch341_recv(struct urb *urb)
{
	struct ch341_spi_dev *ch341 = urb->context;
	struct usb_device *udev = ch341->udev;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -EPIPE:
	case -ESHUTDOWN:
		dev_dbg(&udev->dev, "rx urb terminated with status: %d\n",
			urb->status);
		return;
	default:
		dev_dbg(&udev->dev, "rx urb error: %d\n", urb->status);
		break;
	}
}

static int ch341_config_stream(struct ch341_spi_dev *ch341)
{
	memset(ch341->tx_buf, 0, CH341_PACKET_LENGTH);
	ch341->tx_buf[0] = CH341A_CMD_I2C_STREAM;
	ch341->tx_buf[1] = CH341A_CMD_I2C_STM_SET | CH341A_STM_I2C_100K;
	ch341->tx_buf[2] = CH341A_CMD_I2C_STM_END;

	return usb_bulk_msg(ch341->udev, ch341->write_pipe, ch341->tx_buf, 3,
			    NULL, CH341_DEFAULT_TIMEOUT);
}

static int ch341_enable_pins(struct ch341_spi_dev *ch341, bool enable)
{
	memset(ch341->tx_buf, 0, CH341_PACKET_LENGTH);
	ch341->tx_buf[0] = CH341A_CMD_UIO_STREAM;
	ch341->tx_buf[1] = CH341A_CMD_UIO_STM_OUT | 0x37;
	ch341->tx_buf[2] = CH341A_CMD_UIO_STM_DIR | (enable ? 0x3f : 0x00);
	ch341->tx_buf[3] = CH341A_CMD_UIO_STM_END;

	return usb_bulk_msg(ch341->udev, ch341->write_pipe, ch341->tx_buf, 4,
			    NULL, CH341_DEFAULT_TIMEOUT);
}

static struct spi_board_info chip = {
	.modalias = "spi-ch341a",
};

static int ch341_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *in, *out;
	struct ch341_spi_dev *ch341;
	struct spi_controller *ctrl;
	int ret;

	ret = usb_find_common_endpoints(intf->cur_altsetting, &in, &out, NULL,
					NULL);
	if (ret)
		return ret;

	ctrl = devm_spi_alloc_host(&udev->dev, sizeof(struct ch341_spi_dev));
	if (!ctrl)
		return -ENOMEM;

	ch341 = spi_controller_get_devdata(ctrl);
	ch341->ctrl = ctrl;
	ch341->udev = udev;
	ch341->write_pipe = usb_sndbulkpipe(udev, usb_endpoint_num(out));
	ch341->read_pipe = usb_rcvbulkpipe(udev, usb_endpoint_num(in));

	ch341->rx_len = usb_endpoint_maxp(in);
	ch341->rx_buf = devm_kzalloc(&udev->dev, ch341->rx_len, GFP_KERNEL);
	if (!ch341->rx_buf)
		return -ENOMEM;

	ch341->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ch341->rx_urb)
		return -ENOMEM;

	ch341->tx_buf =
		devm_kzalloc(&udev->dev, CH341_PACKET_LENGTH, GFP_KERNEL);
	if (!ch341->tx_buf)
		return -ENOMEM;

	usb_fill_bulk_urb(ch341->rx_urb, udev, ch341->read_pipe, ch341->rx_buf,
			  ch341->rx_len, ch341_recv, ch341);

	ret = usb_submit_urb(ch341->rx_urb, GFP_KERNEL);
	if (ret) {
		usb_free_urb(ch341->rx_urb);
		return -ENOMEM;
	}

	ctrl->bus_num = -1;
	ctrl->mode_bits = SPI_CPHA;
	ctrl->transfer_one = ch341_transfer_one;
	ctrl->set_cs = ch341_set_cs;
	ctrl->auto_runtime_pm = false;

	usb_set_intfdata(intf, ch341);

	ret = ch341_config_stream(ch341);
	if (ret)
		return ret;

	ret = ch341_enable_pins(ch341, true);
	if (ret)
		return ret;

	ret = spi_register_controller(ctrl);
	if (ret)
		return ret;

	ch341->spidev = spi_new_device(ctrl, &chip);
	if (!ch341->spidev)
		return -ENOMEM;

	return 0;
}

static void ch341_disconnect(struct usb_interface *intf)
{
	struct ch341_spi_dev *ch341 = usb_get_intfdata(intf);

	spi_unregister_device(ch341->spidev);
	spi_unregister_controller(ch341->ctrl);
	ch341_enable_pins(ch341, false);
	usb_free_urb(ch341->rx_urb);
}

static const struct usb_device_id ch341_id_table[] = {
	{ USB_DEVICE(0x1a86, 0x5512) },
	{ }
};
MODULE_DEVICE_TABLE(usb, ch341_id_table);

static struct usb_driver ch341a_usb_driver = {
	.name = "spi-ch341",
	.probe = ch341_probe,
	.disconnect = ch341_disconnect,
	.id_table = ch341_id_table,
};
module_usb_driver(ch341a_usb_driver);

MODULE_AUTHOR("Johannes Thumshirn <jth@kernel.org>");
MODULE_DESCRIPTION("QiHeng Electronics ch341 USB2SPI");
MODULE_LICENSE("GPL v2");
