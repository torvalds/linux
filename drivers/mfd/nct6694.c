// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Nuvoton NCT6694 core driver using USB interface to provide
 * access to the NCT6694 hardware monitoring and control features.
 *
 * The NCT6694 is an integrated controller that provides GPIO, I2C,
 * CAN, WDT, HWMON and RTC management.
 */

#include <linux/bits.h>
#include <linux/interrupt.h>
#include <linux/idr.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/nct6694.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/usb.h>

static const struct mfd_cell nct6694_devs[] = {
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),
	MFD_CELL_NAME("nct6694-gpio"),

	MFD_CELL_NAME("nct6694-i2c"),
	MFD_CELL_NAME("nct6694-i2c"),
	MFD_CELL_NAME("nct6694-i2c"),
	MFD_CELL_NAME("nct6694-i2c"),
	MFD_CELL_NAME("nct6694-i2c"),
	MFD_CELL_NAME("nct6694-i2c"),

	MFD_CELL_NAME("nct6694-canfd"),
	MFD_CELL_NAME("nct6694-canfd"),

	MFD_CELL_NAME("nct6694-wdt"),
	MFD_CELL_NAME("nct6694-wdt"),

	MFD_CELL_NAME("nct6694-hwmon"),

	MFD_CELL_NAME("nct6694-rtc"),
};

static int nct6694_response_err_handling(struct nct6694 *nct6694, unsigned char err_status)
{
	switch (err_status) {
	case NCT6694_NO_ERROR:
		return 0;
	case NCT6694_NOT_SUPPORT_ERROR:
		dev_err(nct6694->dev, "Command is not supported!\n");
		break;
	case NCT6694_NO_RESPONSE_ERROR:
		dev_warn(nct6694->dev, "Command received no response!\n");
		break;
	case NCT6694_TIMEOUT_ERROR:
		dev_warn(nct6694->dev, "Command timed out!\n");
		break;
	case NCT6694_PENDING:
		dev_err(nct6694->dev, "Command is pending!\n");
		break;
	default:
		return -EINVAL;
	}

	return -EIO;
}

/**
 * nct6694_read_msg() - Read message from NCT6694 device
 * @nct6694: NCT6694 device pointer
 * @cmd_hd: command header structure
 * @buf: buffer to store the response data
 *
 * Sends a command to the NCT6694 device and reads the response.
 * The command header is specified in @cmd_hd, and the response
 * data is stored in @buf.
 *
 * Return: Negative value on error or 0 on success.
 */
int nct6694_read_msg(struct nct6694 *nct6694, const struct nct6694_cmd_header *cmd_hd, void *buf)
{
	union nct6694_usb_msg *msg = nct6694->usb_msg;
	struct usb_device *udev = nct6694->udev;
	int tx_len, rx_len, ret;

	guard(mutex)(&nct6694->access_lock);

	memcpy(&msg->cmd_header, cmd_hd, sizeof(*cmd_hd));
	msg->cmd_header.hctrl = NCT6694_HCTRL_GET;

	/* Send command packet to USB device */
	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, NCT6694_BULK_OUT_EP), &msg->cmd_header,
			   sizeof(*msg), &tx_len, NCT6694_URB_TIMEOUT);
	if (ret)
		return ret;

	/* Receive response packet from USB device */
	ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, NCT6694_BULK_IN_EP), &msg->response_header,
			   sizeof(*msg), &rx_len, NCT6694_URB_TIMEOUT);
	if (ret)
		return ret;

	/* Receive data packet from USB device */
	ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, NCT6694_BULK_IN_EP), buf,
			   le16_to_cpu(cmd_hd->len), &rx_len, NCT6694_URB_TIMEOUT);
	if (ret)
		return ret;

	if (rx_len != le16_to_cpu(cmd_hd->len)) {
		dev_err(nct6694->dev, "Expected received length %d, but got %d\n",
			le16_to_cpu(cmd_hd->len), rx_len);
		return -EIO;
	}

	return nct6694_response_err_handling(nct6694, msg->response_header.sts);
}
EXPORT_SYMBOL_GPL(nct6694_read_msg);

/**
 * nct6694_write_msg() - Write message to NCT6694 device
 * @nct6694: NCT6694 device pointer
 * @cmd_hd: command header structure
 * @buf: buffer containing the data to be sent
 *
 * Sends a command to the NCT6694 device and writes the data
 * from @buf. The command header is specified in @cmd_hd.
 *
 * Return: Negative value on error or 0 on success.
 */
int nct6694_write_msg(struct nct6694 *nct6694, const struct nct6694_cmd_header *cmd_hd, void *buf)
{
	union nct6694_usb_msg *msg = nct6694->usb_msg;
	struct usb_device *udev = nct6694->udev;
	int tx_len, rx_len, ret;

	guard(mutex)(&nct6694->access_lock);

	memcpy(&msg->cmd_header, cmd_hd, sizeof(*cmd_hd));
	msg->cmd_header.hctrl = NCT6694_HCTRL_SET;

	/* Send command packet to USB device */
	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, NCT6694_BULK_OUT_EP), &msg->cmd_header,
			   sizeof(*msg), &tx_len, NCT6694_URB_TIMEOUT);
	if (ret)
		return ret;

	/* Send data packet to USB device */
	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, NCT6694_BULK_OUT_EP), buf,
			   le16_to_cpu(cmd_hd->len), &tx_len, NCT6694_URB_TIMEOUT);
	if (ret)
		return ret;

	/* Receive response packet from USB device */
	ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, NCT6694_BULK_IN_EP), &msg->response_header,
			   sizeof(*msg), &rx_len, NCT6694_URB_TIMEOUT);
	if (ret)
		return ret;

	/* Receive data packet from USB device */
	ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, NCT6694_BULK_IN_EP), buf,
			   le16_to_cpu(cmd_hd->len), &rx_len, NCT6694_URB_TIMEOUT);
	if (ret)
		return ret;

	if (rx_len != le16_to_cpu(cmd_hd->len)) {
		dev_err(nct6694->dev, "Expected transmitted length %d, but got %d\n",
			le16_to_cpu(cmd_hd->len), rx_len);
		return -EIO;
	}

	return nct6694_response_err_handling(nct6694, msg->response_header.sts);
}
EXPORT_SYMBOL_GPL(nct6694_write_msg);

static void usb_int_callback(struct urb *urb)
{
	struct nct6694 *nct6694 = urb->context;
	__le32 *status_le = urb->transfer_buffer;
	u32 int_status;
	int ret;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:
		goto resubmit;
	}

	int_status = le32_to_cpu(*status_le);

	while (int_status) {
		int irq = __ffs(int_status);

		generic_handle_irq_safe(irq_find_mapping(nct6694->domain, irq));
		int_status &= ~BIT(irq);
	}

resubmit:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret)
		dev_warn(nct6694->dev, "Failed to resubmit urb, status %pe",  ERR_PTR(ret));
}

static void nct6694_irq_enable(struct irq_data *data)
{
	struct nct6694 *nct6694 = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);

	guard(spinlock_irqsave)(&nct6694->irq_lock);

	nct6694->irq_enable |= BIT(hwirq);
}

static void nct6694_irq_disable(struct irq_data *data)
{
	struct nct6694 *nct6694 = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);

	guard(spinlock_irqsave)(&nct6694->irq_lock);

	nct6694->irq_enable &= ~BIT(hwirq);
}

static const struct irq_chip nct6694_irq_chip = {
	.name = "nct6694-irq",
	.flags = IRQCHIP_SKIP_SET_WAKE,
	.irq_enable = nct6694_irq_enable,
	.irq_disable = nct6694_irq_disable,
};

static int nct6694_irq_domain_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	struct nct6694 *nct6694 = d->host_data;

	irq_set_chip_data(irq, nct6694);
	irq_set_chip_and_handler(irq, &nct6694_irq_chip, handle_simple_irq);

	return 0;
}

static void nct6694_irq_domain_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops nct6694_irq_domain_ops = {
	.map	= nct6694_irq_domain_map,
	.unmap	= nct6694_irq_domain_unmap,
};

static int nct6694_usb_probe(struct usb_interface *iface,
			     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(iface);
	struct usb_endpoint_descriptor *int_endpoint;
	struct usb_host_interface *interface;
	struct device *dev = &iface->dev;
	struct nct6694 *nct6694;
	int ret;

	nct6694 = devm_kzalloc(dev, sizeof(*nct6694), GFP_KERNEL);
	if (!nct6694)
		return -ENOMEM;

	nct6694->usb_msg = devm_kzalloc(dev, sizeof(union nct6694_usb_msg), GFP_KERNEL);
	if (!nct6694->usb_msg)
		return -ENOMEM;

	nct6694->int_buffer = devm_kzalloc(dev, sizeof(*nct6694->int_buffer), GFP_KERNEL);
	if (!nct6694->int_buffer)
		return -ENOMEM;

	nct6694->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!nct6694->int_in_urb)
		return -ENOMEM;

	nct6694->domain = irq_domain_create_simple(NULL, NCT6694_NR_IRQS, 0,
						   &nct6694_irq_domain_ops,
						   nct6694);
	if (!nct6694->domain) {
		ret = -ENODEV;
		goto err_urb;
	}

	nct6694->dev = dev;
	nct6694->udev = udev;

	ida_init(&nct6694->gpio_ida);
	ida_init(&nct6694->i2c_ida);
	ida_init(&nct6694->canfd_ida);
	ida_init(&nct6694->wdt_ida);

	spin_lock_init(&nct6694->irq_lock);

	ret = devm_mutex_init(dev, &nct6694->access_lock);
	if (ret)
		goto err_ida;

	interface = iface->cur_altsetting;

	int_endpoint = &interface->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(int_endpoint)) {
		ret = -ENODEV;
		goto err_ida;
	}

	usb_fill_int_urb(nct6694->int_in_urb, udev, usb_rcvintpipe(udev, NCT6694_INT_IN_EP),
			 nct6694->int_buffer, sizeof(*nct6694->int_buffer), usb_int_callback,
			 nct6694, int_endpoint->bInterval);

	ret = usb_submit_urb(nct6694->int_in_urb, GFP_KERNEL);
	if (ret)
		goto err_ida;

	usb_set_intfdata(iface, nct6694);

	ret = mfd_add_hotplug_devices(dev, nct6694_devs, ARRAY_SIZE(nct6694_devs));
	if (ret)
		goto err_mfd;

	return 0;

err_mfd:
	usb_kill_urb(nct6694->int_in_urb);
err_ida:
	ida_destroy(&nct6694->wdt_ida);
	ida_destroy(&nct6694->canfd_ida);
	ida_destroy(&nct6694->i2c_ida);
	ida_destroy(&nct6694->gpio_ida);
	irq_domain_remove(nct6694->domain);
err_urb:
	usb_free_urb(nct6694->int_in_urb);
	return ret;
}

static void nct6694_usb_disconnect(struct usb_interface *iface)
{
	struct nct6694 *nct6694 = usb_get_intfdata(iface);

	mfd_remove_devices(nct6694->dev);
	usb_kill_urb(nct6694->int_in_urb);
	ida_destroy(&nct6694->wdt_ida);
	ida_destroy(&nct6694->canfd_ida);
	ida_destroy(&nct6694->i2c_ida);
	ida_destroy(&nct6694->gpio_ida);
	irq_domain_remove(nct6694->domain);
	usb_free_urb(nct6694->int_in_urb);
}

static const struct usb_device_id nct6694_ids[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(NCT6694_VENDOR_ID, NCT6694_PRODUCT_ID, 0xFF, 0x00, 0x00) },
	{ }
};
MODULE_DEVICE_TABLE(usb, nct6694_ids);

static struct usb_driver nct6694_usb_driver = {
	.name		= "nct6694",
	.id_table	= nct6694_ids,
	.probe		= nct6694_usb_probe,
	.disconnect	= nct6694_usb_disconnect,
};
module_usb_driver(nct6694_usb_driver);

MODULE_DESCRIPTION("Nuvoton NCT6694 core driver");
MODULE_AUTHOR("Ming Yu <tmyu0@nuvoton.com>");
MODULE_LICENSE("GPL");
