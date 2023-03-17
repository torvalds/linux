// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/usb.h>

#define USB_MARVELL_VID	0x1286

static const struct usb_device_id mvusb_mdio_table[] = {
	{ USB_DEVICE(USB_MARVELL_VID, 0x1fa4) },

	{}
};
MODULE_DEVICE_TABLE(usb, mvusb_mdio_table);

enum {
	MVUSB_CMD_PREAMBLE0,
	MVUSB_CMD_PREAMBLE1,
	MVUSB_CMD_ADDR,
	MVUSB_CMD_VAL,
};

struct mvusb_mdio {
	struct usb_device *udev;
	struct mii_bus *mdio;

	__le16 buf[4];
};

static int mvusb_mdio_read(struct mii_bus *mdio, int dev, int reg)
{
	struct mvusb_mdio *mvusb = mdio->priv;
	int err, alen;

	mvusb->buf[MVUSB_CMD_ADDR] = cpu_to_le16(0xa400 | (dev << 5) | reg);

	err = usb_bulk_msg(mvusb->udev, usb_sndbulkpipe(mvusb->udev, 2),
			   mvusb->buf, 6, &alen, 100);
	if (err)
		return err;

	err = usb_bulk_msg(mvusb->udev, usb_rcvbulkpipe(mvusb->udev, 6),
			   &mvusb->buf[MVUSB_CMD_VAL], 2, &alen, 100);
	if (err)
		return err;

	return le16_to_cpu(mvusb->buf[MVUSB_CMD_VAL]);
}

static int mvusb_mdio_write(struct mii_bus *mdio, int dev, int reg, u16 val)
{
	struct mvusb_mdio *mvusb = mdio->priv;
	int alen;

	mvusb->buf[MVUSB_CMD_ADDR] = cpu_to_le16(0x8000 | (dev << 5) | reg);
	mvusb->buf[MVUSB_CMD_VAL]  = cpu_to_le16(val);

	return usb_bulk_msg(mvusb->udev, usb_sndbulkpipe(mvusb->udev, 2),
			    mvusb->buf, 8, &alen, 100);
}

static int mvusb_mdio_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	struct device *dev = &interface->dev;
	struct mvusb_mdio *mvusb;
	struct mii_bus *mdio;

	mdio = devm_mdiobus_alloc_size(dev, sizeof(*mvusb));
	if (!mdio)
		return -ENOMEM;

	mvusb = mdio->priv;
	mvusb->mdio = mdio;
	mvusb->udev = usb_get_dev(interface_to_usbdev(interface));

	/* Reversed from USB PCAPs, no idea what these mean. */
	mvusb->buf[MVUSB_CMD_PREAMBLE0] = cpu_to_le16(0xe800);
	mvusb->buf[MVUSB_CMD_PREAMBLE1] = cpu_to_le16(0x0001);

	snprintf(mdio->id, MII_BUS_ID_SIZE, "mvusb-%s", dev_name(dev));
	mdio->name = mdio->id;
	mdio->parent = dev;
	mdio->read = mvusb_mdio_read;
	mdio->write = mvusb_mdio_write;

	usb_set_intfdata(interface, mvusb);
	return of_mdiobus_register(mdio, dev->of_node);
}

static void mvusb_mdio_disconnect(struct usb_interface *interface)
{
	struct mvusb_mdio *mvusb = usb_get_intfdata(interface);
	struct usb_device *udev = mvusb->udev;

	mdiobus_unregister(mvusb->mdio);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(udev);
}

static struct usb_driver mvusb_mdio_driver = {
	.name       = "mvusb_mdio",
	.id_table   = mvusb_mdio_table,
	.probe      = mvusb_mdio_probe,
	.disconnect = mvusb_mdio_disconnect,
};

module_usb_driver(mvusb_mdio_driver);

MODULE_AUTHOR("Tobias Waldekranz <tobias@waldekranz.com>");
MODULE_DESCRIPTION("Marvell USB MDIO Adapter");
MODULE_LICENSE("GPL");
