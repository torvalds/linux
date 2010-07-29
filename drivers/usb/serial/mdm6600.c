/*
 * Copyright (C) 2010 Google, Inc.
 * Re-write of Motorola's mdm6600_modem driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

/*
 * TODO check suspend/resume/LP0/LP1
 * TODO remove dummy tiocmget handler
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define MODEM_INTERFACE_NUM 4

static const struct usb_device_id mdm6600_id_table[] = {
	{ USB_DEVICE(0x22b8, 0x2a70) },
	{ },
};
MODULE_DEVICE_TABLE(usb, mdm6600_id_table);

static int mdm6600_dtr_control(struct usb_serial_port *port, int ctrl)
{
	struct usb_device *dev = port->serial->dev;
	struct usb_interface *iface = port->serial->interface;
	u8 request = 0x22;
	u8 request_type = USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT;
	int timeout = HZ * 5;
	int rc;

	rc = usb_autopm_get_interface(iface);
	if (rc < 0) {
		dev_err(&dev->dev, "%s %s autopm failed %d",
			dev_driver_string(&iface->dev), dev_name(&iface->dev),
			rc);
		return rc;
	}

	rc = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), request,
		request_type, ctrl, port->number, NULL, 0, timeout);
	usb_autopm_put_interface(iface);

	return rc;
}

static int mdm6600_tiocmget(struct tty_struct *tty, struct file *file)
{
	/* testing if this ever really gets called */
	BUG_ON(1);
}

static int mdm6600_tiocmset(struct tty_struct *tty, struct file *file,
					unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;

	if (port->number != MODEM_INTERFACE_NUM)
		return 0;
	if (clear & TIOCM_DTR)
		return mdm6600_dtr_control(port, 0);
	if (set & TIOCM_DTR)
		return mdm6600_dtr_control(port, 1);
	return 0;
}

static struct usb_driver mdm6600_usb_driver = {
	.name =		"mdm6600",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	mdm6600_id_table,
	.no_dynamic_id = 	1,
};

static struct usb_serial_driver mdm6600_usb_serial_driver = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"mdm6600",
	},
	.description =		"MDM 6600 modem usb-serial driver",
	.id_table =		mdm6600_id_table,
	.num_ports =		1,  /* TODO: confirm this number is useless */
	.usb_driver =		&mdm6600_usb_driver,
	.tiocmset =		mdm6600_tiocmset,
	.tiocmget =		mdm6600_tiocmget,
};

static int __init mdm6600_init(void)
{
	int retval;

	retval = usb_serial_register(&mdm6600_usb_serial_driver);
	if (retval)
		return retval;
	retval = usb_register(&mdm6600_usb_driver);
	if (retval)
		usb_serial_deregister(&mdm6600_usb_serial_driver);
	return retval;
}

static void __exit mdm6600_exit(void)
{
	usb_deregister(&mdm6600_usb_driver);
	usb_serial_deregister(&mdm6600_usb_serial_driver);
}

module_init(mdm6600_init);
module_exit(mdm6600_exit);
MODULE_LICENSE("GPL");
