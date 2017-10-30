/*
 * comedi/drivers/ni_usb6501.c
 * Comedi driver for National Instruments USB-6501
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2014 Luca Ellero <luca.ellero@brickedbrain.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver: ni_usb6501
 * Description: National Instruments USB-6501 module
 * Devices: [National Instruments] USB-6501 (ni_usb6501)
 * Author: Luca Ellero <luca.ellero@brickedbrain.com>
 * Updated: 8 Sep 2014
 * Status: works
 *
 *
 * Configuration Options:
 * none
 */

/*
 * NI-6501 - USB PROTOCOL DESCRIPTION
 *
 * Every command is composed by two USB packets:
 *	- request (out)
 *	- response (in)
 *
 * Every packet is at least 12 bytes long, here is the meaning of
 * every field (all values are hex):
 *
 *	byte 0 is always 00
 *	byte 1 is always 01
 *	byte 2 is always 00
 *	byte 3 is the total packet length
 *
 *	byte 4 is always 00
 *	byte 5 is the total packet length - 4
 *	byte 6 is always 01
 *	byte 7 is the command
 *
 *	byte 8 is 02 (request) or 00 (response)
 *	byte 9 is 00 (response) or 10 (port request) or 20 (counter request)
 *	byte 10 is always 00
 *	byte 11 is 00 (request) or 02 (response)
 *
 * PORT PACKETS
 *
 *	CMD: 0xE READ_PORT
 *	REQ: 00 01 00 10 00 0C 01 0E 02 10 00 00 00 03 <PORT> 00
 *	RES: 00 01 00 10 00 0C 01 00 00 00 00 02 00 03 <BMAP> 00
 *
 *	CMD: 0xF WRITE_PORT
 *	REQ: 00 01 00 14 00 10 01 0F 02 10 00 00 00 03 <PORT> 00 03 <BMAP> 00 00
 *	RES: 00 01 00 0C 00 08 01 00 00 00 00 02
 *
 *	CMD: 0x12 SET_PORT_DIR (0 = input, 1 = output)
 *	REQ: 00 01 00 18 00 14 01 12 02 10 00 00
 *	     00 05 <PORT 0> <PORT 1> <PORT 2> 00 05 00 00 00 00 00
 *	RES: 00 01 00 0C 00 08 01 00 00 00 00 02
 *
 * COUNTER PACKETS
 *
 *	CMD 0x9: START_COUNTER
 *	REQ: 00 01 00 0C 00 08 01 09 02 20 00 00
 *	RES: 00 01 00 0C 00 08 01 00 00 00 00 02
 *
 *	CMD 0xC: STOP_COUNTER
 *	REQ: 00 01 00 0C 00 08 01 0C 02 20 00 00
 *	RES: 00 01 00 0C 00 08 01 00 00 00 00 02
 *
 *	CMD 0xE: READ_COUNTER
 *	REQ: 00 01 00 0C 00 08 01 0E 02 20 00 00
 *	RES: 00 01 00 10 00 0C 01 00 00 00 00 02 <u32 counter value, Big Endian>
 *
 *	CMD 0xF: WRITE_COUNTER
 *	REQ: 00 01 00 10 00 0C 01 0F 02 20 00 00 <u32 counter value, Big Endian>
 *	RES: 00 01 00 0C 00 08 01 00 00 00 00 02
 *
 *
 *	Please  visit http://www.brickedbrain.com if you need
 *	additional information or have any questions.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "../comedi_usb.h"

#define	NI6501_TIMEOUT	1000

/* Port request packets */
static const u8 READ_PORT_REQUEST[]	= {0x00, 0x01, 0x00, 0x10,
					   0x00, 0x0C, 0x01, 0x0E,
					   0x02, 0x10, 0x00, 0x00,
					   0x00, 0x03, 0x00, 0x00};

static const u8 WRITE_PORT_REQUEST[]	= {0x00, 0x01, 0x00, 0x14,
					   0x00, 0x10, 0x01, 0x0F,
					   0x02, 0x10, 0x00, 0x00,
					   0x00, 0x03, 0x00, 0x00,
					   0x03, 0x00, 0x00, 0x00};

static const u8 SET_PORT_DIR_REQUEST[]	= {0x00, 0x01, 0x00, 0x18,
					   0x00, 0x14, 0x01, 0x12,
					   0x02, 0x10, 0x00, 0x00,
					   0x00, 0x05, 0x00, 0x00,
					   0x00, 0x00, 0x05, 0x00,
					   0x00, 0x00, 0x00, 0x00};

/* Counter request packets */
static const u8 START_COUNTER_REQUEST[]	= {0x00, 0x01, 0x00, 0x0C,
					   0x00, 0x08, 0x01, 0x09,
					   0x02, 0x20, 0x00, 0x00};

static const u8 STOP_COUNTER_REQUEST[]	= {0x00, 0x01, 0x00, 0x0C,
					   0x00, 0x08, 0x01, 0x0C,
					   0x02, 0x20, 0x00, 0x00};

static const u8 READ_COUNTER_REQUEST[]	= {0x00, 0x01, 0x00, 0x0C,
					   0x00, 0x08, 0x01, 0x0E,
					   0x02, 0x20, 0x00, 0x00};

static const u8 WRITE_COUNTER_REQUEST[]	= {0x00, 0x01, 0x00, 0x10,
					   0x00, 0x0C, 0x01, 0x0F,
					   0x02, 0x20, 0x00, 0x00,
					   0x00, 0x00, 0x00, 0x00};

/* Response packets */
static const u8 GENERIC_RESPONSE[]	= {0x00, 0x01, 0x00, 0x0C,
					   0x00, 0x08, 0x01, 0x00,
					   0x00, 0x00, 0x00, 0x02};

static const u8 READ_PORT_RESPONSE[]	= {0x00, 0x01, 0x00, 0x10,
					   0x00, 0x0C, 0x01, 0x00,
					   0x00, 0x00, 0x00, 0x02,
					   0x00, 0x03, 0x00, 0x00};

static const u8 READ_COUNTER_RESPONSE[]	= {0x00, 0x01, 0x00, 0x10,
					   0x00, 0x0C, 0x01, 0x00,
					   0x00, 0x00, 0x00, 0x02,
					   0x00, 0x00, 0x00, 0x00};

enum commands {
	READ_PORT,
	WRITE_PORT,
	SET_PORT_DIR,
	START_COUNTER,
	STOP_COUNTER,
	READ_COUNTER,
	WRITE_COUNTER
};

struct ni6501_private {
	struct usb_endpoint_descriptor *ep_rx;
	struct usb_endpoint_descriptor *ep_tx;
	struct mutex mut;
	u8 *usb_rx_buf;
	u8 *usb_tx_buf;
};

static int ni6501_port_command(struct comedi_device *dev, int command,
			       unsigned int val, u8 *bitmap)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct ni6501_private *devpriv = dev->private;
	int request_size, response_size;
	u8 *tx = devpriv->usb_tx_buf;
	int ret;

	if (command != SET_PORT_DIR && !bitmap)
		return -EINVAL;

	mutex_lock(&devpriv->mut);

	switch (command) {
	case READ_PORT:
		request_size = sizeof(READ_PORT_REQUEST);
		response_size = sizeof(READ_PORT_RESPONSE);
		memcpy(tx, READ_PORT_REQUEST, request_size);
		tx[14] = val & 0xff;
		break;
	case WRITE_PORT:
		request_size = sizeof(WRITE_PORT_REQUEST);
		response_size = sizeof(GENERIC_RESPONSE);
		memcpy(tx, WRITE_PORT_REQUEST, request_size);
		tx[14] = val & 0xff;
		tx[17] = *bitmap;
		break;
	case SET_PORT_DIR:
		request_size = sizeof(SET_PORT_DIR_REQUEST);
		response_size = sizeof(GENERIC_RESPONSE);
		memcpy(tx, SET_PORT_DIR_REQUEST, request_size);
		tx[14] = val & 0xff;
		tx[15] = (val >> 8) & 0xff;
		tx[16] = (val >> 16) & 0xff;
		break;
	default:
		ret = -EINVAL;
		goto end;
	}

	ret = usb_bulk_msg(usb,
			   usb_sndbulkpipe(usb,
					   devpriv->ep_tx->bEndpointAddress),
			   devpriv->usb_tx_buf,
			   request_size,
			   NULL,
			   NI6501_TIMEOUT);
	if (ret)
		goto end;

	ret = usb_bulk_msg(usb,
			   usb_rcvbulkpipe(usb,
					   devpriv->ep_rx->bEndpointAddress),
			   devpriv->usb_rx_buf,
			   response_size,
			   NULL,
			   NI6501_TIMEOUT);
	if (ret)
		goto end;

	/* Check if results are valid */

	if (command == READ_PORT) {
		*bitmap = devpriv->usb_rx_buf[14];
		/* mask bitmap for comparing */
		devpriv->usb_rx_buf[14] = 0x00;

		if (memcmp(devpriv->usb_rx_buf, READ_PORT_RESPONSE,
			   sizeof(READ_PORT_RESPONSE))) {
			ret = -EINVAL;
		}
	} else if (memcmp(devpriv->usb_rx_buf, GENERIC_RESPONSE,
			  sizeof(GENERIC_RESPONSE))) {
		ret = -EINVAL;
	}
end:
	mutex_unlock(&devpriv->mut);

	return ret;
}

static int ni6501_counter_command(struct comedi_device *dev, int command,
				  u32 *val)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct ni6501_private *devpriv = dev->private;
	int request_size, response_size;
	u8 *tx = devpriv->usb_tx_buf;
	int ret;

	if ((command == READ_COUNTER || command ==  WRITE_COUNTER) && !val)
		return -EINVAL;

	mutex_lock(&devpriv->mut);

	switch (command) {
	case START_COUNTER:
		request_size = sizeof(START_COUNTER_REQUEST);
		response_size = sizeof(GENERIC_RESPONSE);
		memcpy(tx, START_COUNTER_REQUEST, request_size);
		break;
	case STOP_COUNTER:
		request_size = sizeof(STOP_COUNTER_REQUEST);
		response_size = sizeof(GENERIC_RESPONSE);
		memcpy(tx, STOP_COUNTER_REQUEST, request_size);
		break;
	case READ_COUNTER:
		request_size = sizeof(READ_COUNTER_REQUEST);
		response_size = sizeof(READ_COUNTER_RESPONSE);
		memcpy(tx, READ_COUNTER_REQUEST, request_size);
		break;
	case WRITE_COUNTER:
		request_size = sizeof(WRITE_COUNTER_REQUEST);
		response_size = sizeof(GENERIC_RESPONSE);
		memcpy(tx, WRITE_COUNTER_REQUEST, request_size);
		/* Setup tx packet: bytes 12,13,14,15 hold the */
		/* u32 counter value (Big Endian)	       */
		*((__be32 *)&tx[12]) = cpu_to_be32(*val);
		break;
	default:
		ret = -EINVAL;
		goto end;
	}

	ret = usb_bulk_msg(usb,
			   usb_sndbulkpipe(usb,
					   devpriv->ep_tx->bEndpointAddress),
			   devpriv->usb_tx_buf,
			   request_size,
			   NULL,
			   NI6501_TIMEOUT);
	if (ret)
		goto end;

	ret = usb_bulk_msg(usb,
			   usb_rcvbulkpipe(usb,
					   devpriv->ep_rx->bEndpointAddress),
			   devpriv->usb_rx_buf,
			   response_size,
			   NULL,
			   NI6501_TIMEOUT);
	if (ret)
		goto end;

	/* Check if results are valid */

	if (command == READ_COUNTER) {
		int i;

		/* Read counter value: bytes 12,13,14,15 of rx packet */
		/* hold the u32 counter value (Big Endian)	      */
		*val = be32_to_cpu(*((__be32 *)&devpriv->usb_rx_buf[12]));

		/* mask counter value for comparing */
		for (i = 12; i < sizeof(READ_COUNTER_RESPONSE); ++i)
			devpriv->usb_rx_buf[i] = 0x00;

		if (memcmp(devpriv->usb_rx_buf, READ_COUNTER_RESPONSE,
			   sizeof(READ_COUNTER_RESPONSE))) {
			ret = -EINVAL;
		}
	} else if (memcmp(devpriv->usb_rx_buf, GENERIC_RESPONSE,
			  sizeof(GENERIC_RESPONSE))) {
		ret = -EINVAL;
	}
end:
	mutex_unlock(&devpriv->mut);

	return ret;
}

static int ni6501_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	ret = ni6501_port_command(dev, SET_PORT_DIR, s->io_bits, NULL);
	if (ret)
		return ret;

	return insn->n;
}

static int ni6501_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int mask;
	int ret;
	u8 port;
	u8 bitmap;

	mask = comedi_dio_update_state(s, data);

	for (port = 0; port < 3; port++) {
		if (mask & (0xFF << port * 8)) {
			bitmap = (s->state >> port * 8) & 0xFF;
			ret = ni6501_port_command(dev, WRITE_PORT,
						  port, &bitmap);
			if (ret)
				return ret;
		}
	}

	data[1] = 0;

	for (port = 0; port < 3; port++) {
		ret = ni6501_port_command(dev, READ_PORT, port, &bitmap);
		if (ret)
			return ret;
		data[1] |= bitmap << port * 8;
	}

	return insn->n;
}

static int ni6501_cnt_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	int ret;
	u32 val = 0;

	switch (data[0]) {
	case INSN_CONFIG_ARM:
		ret = ni6501_counter_command(dev, START_COUNTER, NULL);
		break;
	case INSN_CONFIG_DISARM:
		ret = ni6501_counter_command(dev, STOP_COUNTER, NULL);
		break;
	case INSN_CONFIG_RESET:
		ret = ni6501_counter_command(dev, STOP_COUNTER, NULL);
		if (ret)
			break;
		ret = ni6501_counter_command(dev, WRITE_COUNTER, &val);
		break;
	default:
		return -EINVAL;
	}

	return ret ? ret : insn->n;
}

static int ni6501_cnt_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	int ret;
	u32 val;
	unsigned int i;

	for (i = 0; i < insn->n; i++) {
		ret = ni6501_counter_command(dev, READ_COUNTER,	&val);
		if (ret)
			return ret;
		data[i] = val;
	}

	return insn->n;
}

static int ni6501_cnt_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	int ret;

	if (insn->n) {
		u32 val = data[insn->n - 1];

		ret = ni6501_counter_command(dev, WRITE_COUNTER, &val);
		if (ret)
			return ret;
	}

	return insn->n;
}

static int ni6501_alloc_usb_buffers(struct comedi_device *dev)
{
	struct ni6501_private *devpriv = dev->private;
	size_t size;

	size = usb_endpoint_maxp(devpriv->ep_rx);
	devpriv->usb_rx_buf = kzalloc(size, GFP_KERNEL);
	if (!devpriv->usb_rx_buf)
		return -ENOMEM;

	size = usb_endpoint_maxp(devpriv->ep_tx);
	devpriv->usb_tx_buf = kzalloc(size, GFP_KERNEL);
	if (!devpriv->usb_tx_buf) {
		kfree(devpriv->usb_rx_buf);
		return -ENOMEM;
	}

	return 0;
}

static int ni6501_find_endpoints(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct ni6501_private *devpriv = dev->private;
	struct usb_host_interface *iface_desc = intf->cur_altsetting;
	struct usb_endpoint_descriptor *ep_desc;
	int i;

	if (iface_desc->desc.bNumEndpoints != 2) {
		dev_err(dev->class_dev, "Wrong number of endpoints\n");
		return -ENODEV;
	}

	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		ep_desc = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep_desc)) {
			if (!devpriv->ep_rx)
				devpriv->ep_rx = ep_desc;
			continue;
		}

		if (usb_endpoint_is_bulk_out(ep_desc)) {
			if (!devpriv->ep_tx)
				devpriv->ep_tx = ep_desc;
			continue;
		}
	}

	if (!devpriv->ep_rx || !devpriv->ep_tx)
		return -ENODEV;

	return 0;
}

static int ni6501_auto_attach(struct comedi_device *dev,
			      unsigned long context)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct ni6501_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = ni6501_find_endpoints(dev);
	if (ret)
		return ret;

	ret = ni6501_alloc_usb_buffers(dev);
	if (ret)
		return ret;

	mutex_init(&devpriv->mut);
	usb_set_intfdata(intf, devpriv);

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/* Digital Input/Output subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 24;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= ni6501_dio_insn_bits;
	s->insn_config	= ni6501_dio_insn_config;

	/* Counter subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_LSAMPL;
	s->n_chan	= 1;
	s->maxdata	= 0xffffffff;
	s->insn_read	= ni6501_cnt_insn_read;
	s->insn_write	= ni6501_cnt_insn_write;
	s->insn_config	= ni6501_cnt_insn_config;

	return 0;
}

static void ni6501_detach(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct ni6501_private *devpriv = dev->private;

	if (!devpriv)
		return;

	mutex_lock(&devpriv->mut);

	usb_set_intfdata(intf, NULL);

	kfree(devpriv->usb_rx_buf);
	kfree(devpriv->usb_tx_buf);

	mutex_unlock(&devpriv->mut);
}

static struct comedi_driver ni6501_driver = {
	.module		= THIS_MODULE,
	.driver_name	= "ni6501",
	.auto_attach	= ni6501_auto_attach,
	.detach		= ni6501_detach,
};

static int ni6501_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	return comedi_usb_auto_config(intf, &ni6501_driver, id->driver_info);
}

static const struct usb_device_id ni6501_usb_table[] = {
	{ USB_DEVICE(0x3923, 0x718a) },
	{ }
};
MODULE_DEVICE_TABLE(usb, ni6501_usb_table);

static struct usb_driver ni6501_usb_driver = {
	.name		= "ni6501",
	.id_table	= ni6501_usb_table,
	.probe		= ni6501_usb_probe,
	.disconnect	= comedi_usb_auto_unconfig,
};
module_comedi_usb_driver(ni6501_driver, ni6501_usb_driver);

MODULE_AUTHOR("Luca Ellero");
MODULE_DESCRIPTION("Comedi driver for National Instruments USB-6501");
MODULE_LICENSE("GPL");
