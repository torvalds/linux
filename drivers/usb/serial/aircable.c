/*
 * AIRcable USB Bluetooth Dongle Driver.
 *
 * Copyright (C) 2006 Manuel Francisco Naranjo (naranjo.manuel@gmail.com)
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * The device works as an standard CDC device, it has 2 interfaces, the first
 * one is for firmware access and the second is the serial one.
 * The protocol is very simply, there are two posibilities reading or writing.
 * When writting the first urb must have a Header that starts with 0x20 0x29 the
 * next two bytes must say how much data will be sended.
 * When reading the process is almost equal except that the header starts with
 * 0x00 0x20.
 *
 * The device simply need some stuff to understand data comming from the usb
 * buffer: The First and Second byte is used for a Header, the Third and Fourth
 * tells the  device the amount of information the package holds.
 * Packages are 60 bytes long Header Stuff.
 * When writting to the device the first two bytes of the header are 0x20 0x29
 * When reading the bytes are 0x00 0x20, or 0x00 0x10, there is an strange
 * situation, when too much data arrives to the device because it sends the data
 * but with out the header. I will use a simply hack to override this situation,
 * if there is data coming that does not contain any header, then that is data
 * that must go directly to the tty, as there is no documentation about if there
 * is any other control code, I will simply check for the first
 * one.
 *
 * The driver registers himself with the USB-serial core and the USB Core. I had
 * to implement a probe function agains USB-serial, because other way, the
 * driver was attaching himself to both interfaces. I have tryed with different
 * configurations of usb_serial_driver with out exit, only the probe function
 * could handle this correctly.
 *
 * I have taken some info from a Greg Kroah-Hartman article:
 * http://www.linuxjournal.com/article/6573
 * And from Linux Device Driver Kit CD, which is a great work, the authors taken
 * the work to recompile lots of information an knowladge in drivers development
 * and made it all avaible inside a cd.
 * URL: http://kernel.org/pub/linux/kernel/people/gregkh/ddk/
 *
 */

#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/circ_buf.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

static int debug;

/* Vendor and Product ID */
#define AIRCABLE_VID		0x16CA
#define AIRCABLE_USB_PID	0x1502

/* write buffer size defines */
#define AIRCABLE_BUF_SIZE	2048

/* Protocol Stuff */
#define HCI_HEADER_LENGTH	0x4
#define TX_HEADER_0		0x20
#define TX_HEADER_1		0x29
#define RX_HEADER_0		0x00
#define RX_HEADER_1		0x20
#define MAX_HCI_FRAMESIZE	60
#define HCI_COMPLETE_FRAME	64

/* rx_flags */
#define THROTTLED		0x01
#define ACTUALLY_THROTTLED	0x02

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.0b2"
#define DRIVER_AUTHOR "Naranjo, Manuel Francisco <naranjo.manuel@gmail.com>"
#define DRIVER_DESC "AIRcable USB Driver"

/* ID table that will be registered with USB core */
static struct usb_device_id id_table [] = {
	{ USB_DEVICE(AIRCABLE_VID, AIRCABLE_USB_PID) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);


/* Internal Structure */
struct aircable_private {
	spinlock_t rx_lock;		/* spinlock for the receive lines */
	struct circ_buf *tx_buf;	/* write buffer */
	struct circ_buf *rx_buf;	/* read buffer */
	int rx_flags;			/* for throttilng */
	struct work_struct rx_work;	/* work cue for the receiving line */
	struct usb_serial_port *port;	/* USB port with which associated */
};

/* Private methods */

/* Circular Buffer Methods, code from ti_usb_3410_5052 used */
/*
 * serial_buf_clear
 *
 * Clear out all data in the circular buffer.
 */
static void serial_buf_clear(struct circ_buf *cb)
{
	cb->head = cb->tail = 0;
}

/*
 * serial_buf_alloc
 *
 * Allocate a circular buffer and all associated memory.
 */
static struct circ_buf *serial_buf_alloc(void)
{
	struct circ_buf *cb;
	cb = kmalloc(sizeof(struct circ_buf), GFP_KERNEL);
	if (cb == NULL)
		return NULL;
	cb->buf = kmalloc(AIRCABLE_BUF_SIZE, GFP_KERNEL);
	if (cb->buf == NULL) {
		kfree(cb);
		return NULL;
	}
	serial_buf_clear(cb);
	return cb;
}

/*
 * serial_buf_free
 *
 * Free the buffer and all associated memory.
 */
static void serial_buf_free(struct circ_buf *cb)
{
	kfree(cb->buf);
	kfree(cb);
}

/*
 * serial_buf_data_avail
 *
 * Return the number of bytes of data available in the circular
 * buffer.
 */
static int serial_buf_data_avail(struct circ_buf *cb)
{
	return CIRC_CNT(cb->head,cb->tail,AIRCABLE_BUF_SIZE);
}

/*
 * serial_buf_put
 *
 * Copy data data from a user buffer and put it into the circular buffer.
 * Restrict to the amount of space available.
 *
 * Return the number of bytes copied.
 */
static int serial_buf_put(struct circ_buf *cb, const char *buf, int count)
{
	int c, ret = 0;
	while (1) {
		c = CIRC_SPACE_TO_END(cb->head, cb->tail, AIRCABLE_BUF_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
		memcpy(cb->buf + cb->head, buf, c);
		cb->head = (cb->head + c) & (AIRCABLE_BUF_SIZE-1);
		buf += c;
		count -= c;
		ret= c;
	}
	return ret;
}

/*
 * serial_buf_get
 *
 * Get data from the circular buffer and copy to the given buffer.
 * Restrict to the amount of data available.
 *
 * Return the number of bytes copied.
 */
static int serial_buf_get(struct circ_buf *cb, char *buf, int count)
{
	int c, ret = 0;
	while (1) {
		c = CIRC_CNT_TO_END(cb->head, cb->tail, AIRCABLE_BUF_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
		memcpy(buf, cb->buf + cb->tail, c);
		cb->tail = (cb->tail + c) & (AIRCABLE_BUF_SIZE-1);
		buf += c;
		count -= c;
		ret= c;
	}
	return ret;
}

/* End of circula buffer methods */

static void aircable_send(struct usb_serial_port *port)
{
	int count, result;
	struct aircable_private *priv = usb_get_serial_port_data(port);
	unsigned char* buf;
	dbg("%s - port %d", __FUNCTION__, port->number);
	if (port->write_urb_busy)
		return;

	count = min(serial_buf_data_avail(priv->tx_buf), MAX_HCI_FRAMESIZE);
	if (count == 0)
		return;

	buf = kzalloc(count + HCI_HEADER_LENGTH, GFP_ATOMIC);
	if (!buf) {
		err("%s- kzalloc(%d) failed.", __FUNCTION__,
		    count + HCI_HEADER_LENGTH);
		return;
	}

	buf[0] = TX_HEADER_0;
	buf[1] = TX_HEADER_1;
	buf[2] = (unsigned char)count;
	buf[3] = (unsigned char)(count >> 8);
	serial_buf_get(priv->tx_buf,buf + HCI_HEADER_LENGTH, MAX_HCI_FRAMESIZE);

	memcpy(port->write_urb->transfer_buffer, buf,
	       count + HCI_HEADER_LENGTH);

	kfree(buf);
	port->write_urb_busy = 1;
	usb_serial_debug_data(debug, &port->dev, __FUNCTION__,
			      count + HCI_HEADER_LENGTH,
			      port->write_urb->transfer_buffer);
	port->write_urb->transfer_buffer_length = count + HCI_HEADER_LENGTH;
	port->write_urb->dev = port->serial->dev;
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);

	if (result) {
		dev_err(&port->dev,
			"%s - failed submitting write urb, error %d\n",
			__FUNCTION__, result);
		port->write_urb_busy = 0;
	}

	schedule_work(&port->work);
}

static void aircable_read(struct work_struct *work)
{
	struct aircable_private *priv =
		container_of(work, struct aircable_private, rx_work);
	struct usb_serial_port *port = priv->port;
	struct tty_struct *tty;
	unsigned char *data;
	int count;
	if (priv->rx_flags & THROTTLED){
		if (priv->rx_flags & ACTUALLY_THROTTLED)
			schedule_work(&priv->rx_work);
		return;
	}

	/* By now I will flush data to the tty in packages of no more than
	 * 64 bytes, to ensure I do not get throttled.
	 * Ask USB mailing list for better aproach.
	 */
	tty = port->tty;

	if (!tty) {
		schedule_work(&priv->rx_work);
		err("%s - No tty available", __FUNCTION__);
		return ;
	}

	count = min(64, serial_buf_data_avail(priv->rx_buf));

	if (count <= 0)
		return; //We have finished sending everything.

	tty_prepare_flip_string(tty, &data, count);
	if (!data){
		err("%s- kzalloc(%d) failed.", __FUNCTION__, count);
		return;
	}

	serial_buf_get(priv->rx_buf, data, count);

	tty_flip_buffer_push(tty);

	if (serial_buf_data_avail(priv->rx_buf))
		schedule_work(&priv->rx_work);

	return;
}
/* End of private methods */

static int aircable_probe(struct usb_serial *serial,
			  const struct usb_device_id *id)
{
	struct usb_host_interface *iface_desc = serial->interface->cur_altsetting;
	struct usb_endpoint_descriptor *endpoint;
	int num_bulk_out=0;
	int i;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (usb_endpoint_is_bulk_out(endpoint)) {
			dbg("found bulk out on endpoint %d", i);
			++num_bulk_out;
		}
	}

	if (num_bulk_out == 0) {
		dbg("Invalid interface, discarding");
		return -ENODEV;
	}

	return 0;
}

static int aircable_attach (struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	struct aircable_private *priv;

	priv = kzalloc(sizeof(struct aircable_private), GFP_KERNEL);
	if (!priv){
		err("%s- kmalloc(%Zd) failed.", __FUNCTION__,
			sizeof(struct aircable_private));
		return -ENOMEM;
	}

	/* Allocation of Circular Buffers */
	priv->tx_buf = serial_buf_alloc();
	if (priv->tx_buf == NULL) {
		kfree(priv);
		return -ENOMEM;
	}

	priv->rx_buf = serial_buf_alloc();
	if (priv->rx_buf == NULL) {
		kfree(priv->tx_buf);
		kfree(priv);
		return -ENOMEM;
	}

	priv->rx_flags &= ~(THROTTLED | ACTUALLY_THROTTLED);
	priv->port = port;
	INIT_WORK(&priv->rx_work, aircable_read);

	usb_set_serial_port_data(serial->port[0], priv);

	return 0;
}

static void aircable_shutdown(struct usb_serial *serial)
{

	struct usb_serial_port *port = serial->port[0];
	struct aircable_private *priv = usb_get_serial_port_data(port);

	dbg("%s", __FUNCTION__);

	if (priv) {
		serial_buf_free(priv->tx_buf);
		serial_buf_free(priv->rx_buf);
		usb_set_serial_port_data(port, NULL);
		kfree(priv);
	}
}

static int aircable_write_room(struct usb_serial_port *port)
{
	struct aircable_private *priv = usb_get_serial_port_data(port);
	return serial_buf_data_avail(priv->tx_buf);
}

static int aircable_write(struct usb_serial_port *port,
			  const unsigned char *source, int count)
{
	struct aircable_private *priv = usb_get_serial_port_data(port);
	int temp;

	dbg("%s - port %d, %d bytes", __FUNCTION__, port->number, count);

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, count, source);

	if (!count){
		dbg("%s - write request of 0 bytes", __FUNCTION__);
		return count;
	}

	temp = serial_buf_put(priv->tx_buf, source, count);

	aircable_send(port);

	if (count > AIRCABLE_BUF_SIZE)
		count = AIRCABLE_BUF_SIZE;

	return count;

}

static void aircable_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int result;

	dbg("%s - urb->status: %d", __FUNCTION__ , urb->status);

	/* This has been taken from cypress_m8.c cypress_write_int_callback */
	switch (urb->status) {
		case 0:
			/* success */
			break;
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/* this urb is terminated, clean up */
			dbg("%s - urb shutting down with status: %d",
			    __FUNCTION__, urb->status);
			port->write_urb_busy = 0;
			return;
		default:
			/* error in the urb, so we have to resubmit it */
			dbg("%s - Overflow in write", __FUNCTION__);
			dbg("%s - nonzero write bulk status received: %d",
			    __FUNCTION__, urb->status);
			port->write_urb->transfer_buffer_length = 1;
			port->write_urb->dev = port->serial->dev;
			result = usb_submit_urb(port->write_urb, GFP_KERNEL);
			if (result)
				dev_err(&urb->dev->dev,
					"%s - failed resubmitting write urb, error %d\n",
					__FUNCTION__, result);
			else
				return;
	}

	port->write_urb_busy = 0;

	aircable_send(port);
}

static void aircable_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct aircable_private *priv = usb_get_serial_port_data(port);
	struct tty_struct *tty;
	unsigned long no_packages, remaining, package_length, i;
	int result, shift = 0;
	unsigned char *temp;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (urb->status) {
		dbg("%s - urb->status = %d", __FUNCTION__, urb->status);
		if (!port->open_count) {
			dbg("%s - port is closed, exiting.", __FUNCTION__);
			return;
		}
		if (urb->status == -EPROTO) {
			dbg("%s - caught -EPROTO, resubmitting the urb",
			    __FUNCTION__);
			usb_fill_bulk_urb(port->read_urb, port->serial->dev,
					  usb_rcvbulkpipe(port->serial->dev,
					  		  port->bulk_in_endpointAddress),
					  port->read_urb->transfer_buffer,
					  port->read_urb->transfer_buffer_length,
					  aircable_read_bulk_callback, port);

			result = usb_submit_urb(urb, GFP_ATOMIC);
			if (result)
				dev_err(&urb->dev->dev,
					"%s - failed resubmitting read urb, error %d\n",
					__FUNCTION__, result);
			return;
		}
		dbg("%s - unable to handle the error, exiting.", __FUNCTION__);
		return;
	}

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__,
				urb->actual_length,urb->transfer_buffer);

	tty = port->tty;
	if (tty && urb->actual_length) {
		if (urb->actual_length <= 2) {
			/* This is an incomplete package */
			serial_buf_put(priv->rx_buf, urb->transfer_buffer,
				       urb->actual_length);
		} else {
			temp = urb->transfer_buffer;
			if (temp[0] == RX_HEADER_0)
				shift = HCI_HEADER_LENGTH;

			remaining = urb->actual_length;
			no_packages = urb->actual_length / (HCI_COMPLETE_FRAME);

			if (urb->actual_length % HCI_COMPLETE_FRAME != 0)
				no_packages+=1;

			for (i = 0; i < no_packages ;i++) {
				if (remaining > (HCI_COMPLETE_FRAME))
					package_length = HCI_COMPLETE_FRAME;
				else
					package_length = remaining;
				remaining -= package_length;

				serial_buf_put(priv->rx_buf,
					urb->transfer_buffer + shift +
					(HCI_COMPLETE_FRAME) * (i),
					package_length - shift);
			}
		}
		aircable_read(&priv->rx_work);
	}

	/* Schedule the next read _if_ we are still open */
	if (port->open_count) {
		usb_fill_bulk_urb(port->read_urb, port->serial->dev,
				  usb_rcvbulkpipe(port->serial->dev,
				  		  port->bulk_in_endpointAddress),
				  port->read_urb->transfer_buffer,
				  port->read_urb->transfer_buffer_length,
				  aircable_read_bulk_callback, port);

		result = usb_submit_urb(urb, GFP_ATOMIC);
		if (result)
			dev_err(&urb->dev->dev,
				"%s - failed resubmitting read urb, error %d\n",
				__FUNCTION__, result);
	}

	return;
}

/* Based on ftdi_sio.c throttle */
static void aircable_throttle(struct usb_serial_port *port)
{
	struct aircable_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->rx_lock, flags);
	priv->rx_flags |= THROTTLED;
	spin_unlock_irqrestore(&priv->rx_lock, flags);
}

/* Based on ftdi_sio.c unthrottle */
static void aircable_unthrottle(struct usb_serial_port *port)
{
	struct aircable_private *priv = usb_get_serial_port_data(port);
	int actually_throttled;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->rx_lock, flags);
	actually_throttled = priv->rx_flags & ACTUALLY_THROTTLED;
	priv->rx_flags &= ~(THROTTLED | ACTUALLY_THROTTLED);
	spin_unlock_irqrestore(&priv->rx_lock, flags);

	if (actually_throttled)
		schedule_work(&priv->rx_work);
}

static struct usb_driver aircable_driver = {
	.name =		"aircable",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id =	1,
};

static struct usb_serial_driver aircable_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"aircable",
	},
	.usb_driver = 		&aircable_driver,
	.id_table = 		id_table,
	.num_ports =		1,
	.attach =		aircable_attach,
	.probe =		aircable_probe,
	.shutdown =		aircable_shutdown,
	.write =		aircable_write,
	.write_room =		aircable_write_room,
	.write_bulk_callback =	aircable_write_bulk_callback,
	.read_bulk_callback =	aircable_read_bulk_callback,
	.throttle =		aircable_throttle,
	.unthrottle =		aircable_unthrottle,
};

static int __init aircable_init (void)
{
	int retval;
	retval = usb_serial_register(&aircable_device);
	if (retval)
		goto failed_serial_register;
	retval = usb_register(&aircable_driver);
	if (retval)
		goto failed_usb_register;
	return 0;

failed_serial_register:
	usb_serial_deregister(&aircable_device);
failed_usb_register:
	return retval;
}

static void __exit aircable_exit (void)
{
	usb_deregister(&aircable_driver);
	usb_serial_deregister(&aircable_device);
}

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

module_init(aircable_init);
module_exit(aircable_exit);

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
