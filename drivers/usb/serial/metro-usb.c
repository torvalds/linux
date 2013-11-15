/*
  Some of this code is credited to Linux USB open source files that are
  distributed with Linux.

  Copyright:	2007 Metrologic Instruments. All rights reserved.
  Copyright:	2011 Azimut Ltd. <http://azimutrzn.ru/>
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb/serial.h>

#define DRIVER_DESC "Metrologic Instruments Inc. - USB-POS driver"

/* Product information. */
#define FOCUS_VENDOR_ID			0x0C2E
#define FOCUS_PRODUCT_ID_BI		0x0720
#define FOCUS_PRODUCT_ID_UNI		0x0700

#define METROUSB_SET_REQUEST_TYPE	0x40
#define METROUSB_SET_MODEM_CTRL_REQUEST	10
#define METROUSB_SET_BREAK_REQUEST	0x40
#define METROUSB_MCR_NONE		0x08	/* Deactivate DTR and RTS. */
#define METROUSB_MCR_RTS		0x0a	/* Activate RTS. */
#define METROUSB_MCR_DTR		0x09	/* Activate DTR. */
#define WDR_TIMEOUT			5000	/* default urb timeout. */

/* Private data structure. */
struct metrousb_private {
	spinlock_t lock;
	int throttled;
	unsigned long control_state;
};

/* Device table list. */
static struct usb_device_id id_table[] = {
	{ USB_DEVICE(FOCUS_VENDOR_ID, FOCUS_PRODUCT_ID_BI) },
	{ USB_DEVICE(FOCUS_VENDOR_ID, FOCUS_PRODUCT_ID_UNI) },
	{ }, /* Terminating entry. */
};
MODULE_DEVICE_TABLE(usb, id_table);

/* UNI-Directional mode commands for device configure */
#define UNI_CMD_OPEN	0x80
#define UNI_CMD_CLOSE	0xFF

inline int metrousb_is_unidirectional_mode(struct usb_serial_port *port)
{
	__u16 product_id = le16_to_cpu(
		port->serial->dev->descriptor.idProduct);

	return product_id == FOCUS_PRODUCT_ID_UNI;
}

static int metrousb_send_unidirectional_cmd(u8 cmd, struct usb_serial_port *port)
{
	int ret;
	int actual_len;
	u8 *buffer_cmd = NULL;

	if (!metrousb_is_unidirectional_mode(port))
		return 0;

	buffer_cmd = kzalloc(sizeof(cmd), GFP_KERNEL);
	if (!buffer_cmd)
		return -ENOMEM;

	*buffer_cmd = cmd;

	ret = usb_interrupt_msg(port->serial->dev,
		usb_sndintpipe(port->serial->dev, port->interrupt_out_endpointAddress),
		buffer_cmd, sizeof(cmd),
		&actual_len, USB_CTRL_SET_TIMEOUT);

	kfree(buffer_cmd);

	if (ret < 0)
		return ret;
	else if (actual_len != sizeof(cmd))
		return -EIO;
	return 0;
}

static void metrousb_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct metrousb_private *metro_priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	int throttled = 0;
	int result = 0;
	unsigned long flags = 0;

	dev_dbg(&port->dev, "%s\n", __func__);

	switch (urb->status) {
	case 0:
		/* Success status, read from the port. */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* urb has been terminated. */
		dev_dbg(&port->dev,
			"%s - urb shutting down, error code=%d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(&port->dev,
			"%s - non-zero urb received, error code=%d\n",
			__func__, urb->status);
		goto exit;
	}


	/* Set the data read from the usb port into the serial port buffer. */
	if (urb->actual_length) {
		/* Loop through the data copying each byte to the tty layer. */
		tty_insert_flip_string(&port->port, data, urb->actual_length);

		/* Force the data to the tty layer. */
		tty_flip_buffer_push(&port->port);
	}

	/* Set any port variables. */
	spin_lock_irqsave(&metro_priv->lock, flags);
	throttled = metro_priv->throttled;
	spin_unlock_irqrestore(&metro_priv->lock, flags);

	/* Continue trying to read if set. */
	if (!throttled) {
		usb_fill_int_urb(port->interrupt_in_urb, port->serial->dev,
				 usb_rcvintpipe(port->serial->dev, port->interrupt_in_endpointAddress),
				 port->interrupt_in_urb->transfer_buffer,
				 port->interrupt_in_urb->transfer_buffer_length,
				 metrousb_read_int_callback, port, 1);

		result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);

		if (result)
			dev_err(&port->dev,
				"%s - failed submitting interrupt in urb, error code=%d\n",
				__func__, result);
	}
	return;

exit:
	/* Try to resubmit the urb. */
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev,
			"%s - failed submitting interrupt in urb, error code=%d\n",
			__func__, result);
}

static void metrousb_write_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;

	dev_warn(&port->dev, "%s not implemented yet.\n",
		__func__);
}

static void metrousb_cleanup(struct usb_serial_port *port)
{
	dev_dbg(&port->dev, "%s\n", __func__);

	usb_unlink_urb(port->interrupt_in_urb);
	usb_kill_urb(port->interrupt_in_urb);

	metrousb_send_unidirectional_cmd(UNI_CMD_CLOSE, port);
}

static int metrousb_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct metrousb_private *metro_priv = usb_get_serial_port_data(port);
	unsigned long flags = 0;
	int result = 0;

	dev_dbg(&port->dev, "%s\n", __func__);

	/* Make sure the urb is initialized. */
	if (!port->interrupt_in_urb) {
		dev_err(&port->dev, "%s - interrupt urb not initialized\n",
			__func__);
		return -ENODEV;
	}

	/* Set the private data information for the port. */
	spin_lock_irqsave(&metro_priv->lock, flags);
	metro_priv->control_state = 0;
	metro_priv->throttled = 0;
	spin_unlock_irqrestore(&metro_priv->lock, flags);

	/* Clear the urb pipe. */
	usb_clear_halt(serial->dev, port->interrupt_in_urb->pipe);

	/* Start reading from the device */
	usb_fill_int_urb(port->interrupt_in_urb, serial->dev,
			  usb_rcvintpipe(serial->dev, port->interrupt_in_endpointAddress),
			   port->interrupt_in_urb->transfer_buffer,
			   port->interrupt_in_urb->transfer_buffer_length,
			   metrousb_read_int_callback, port, 1);
	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);

	if (result) {
		dev_err(&port->dev,
			"%s - failed submitting interrupt in urb, error code=%d\n",
			__func__, result);
		goto exit;
	}

	/* Send activate cmd to device */
	result = metrousb_send_unidirectional_cmd(UNI_CMD_OPEN, port);
	if (result) {
		dev_err(&port->dev,
			"%s - failed to configure device, error code=%d\n",
			__func__, result);
		goto exit;
	}

	dev_dbg(&port->dev, "%s - port open\n", __func__);
exit:
	return result;
}

static int metrousb_set_modem_ctrl(struct usb_serial *serial, unsigned int control_state)
{
	int retval = 0;
	unsigned char mcr = METROUSB_MCR_NONE;

	dev_dbg(&serial->dev->dev, "%s - control state = %d\n",
		__func__, control_state);

	/* Set the modem control value. */
	if (control_state & TIOCM_DTR)
		mcr |= METROUSB_MCR_DTR;
	if (control_state & TIOCM_RTS)
		mcr |= METROUSB_MCR_RTS;

	/* Send the command to the usb port. */
	retval = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				METROUSB_SET_REQUEST_TYPE, METROUSB_SET_MODEM_CTRL_REQUEST,
				control_state, 0, NULL, 0, WDR_TIMEOUT);
	if (retval < 0)
		dev_err(&serial->dev->dev,
			"%s - set modem ctrl=0x%x failed, error code=%d\n",
			__func__, mcr, retval);

	return retval;
}

static int metrousb_port_probe(struct usb_serial_port *port)
{
	struct metrousb_private *metro_priv;

	metro_priv = kzalloc(sizeof(*metro_priv), GFP_KERNEL);
	if (!metro_priv)
		return -ENOMEM;

	spin_lock_init(&metro_priv->lock);

	usb_set_serial_port_data(port, metro_priv);

	return 0;
}

static int metrousb_port_remove(struct usb_serial_port *port)
{
	struct metrousb_private *metro_priv;

	metro_priv = usb_get_serial_port_data(port);
	kfree(metro_priv);

	return 0;
}

static void metrousb_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct metrousb_private *metro_priv = usb_get_serial_port_data(port);
	unsigned long flags = 0;

	dev_dbg(tty->dev, "%s\n", __func__);

	/* Set the private information for the port to stop reading data. */
	spin_lock_irqsave(&metro_priv->lock, flags);
	metro_priv->throttled = 1;
	spin_unlock_irqrestore(&metro_priv->lock, flags);
}

static int metrousb_tiocmget(struct tty_struct *tty)
{
	unsigned long control_state = 0;
	struct usb_serial_port *port = tty->driver_data;
	struct metrousb_private *metro_priv = usb_get_serial_port_data(port);
	unsigned long flags = 0;

	dev_dbg(tty->dev, "%s\n", __func__);

	spin_lock_irqsave(&metro_priv->lock, flags);
	control_state = metro_priv->control_state;
	spin_unlock_irqrestore(&metro_priv->lock, flags);

	return control_state;
}

static int metrousb_tiocmset(struct tty_struct *tty,
			     unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	struct metrousb_private *metro_priv = usb_get_serial_port_data(port);
	unsigned long flags = 0;
	unsigned long control_state = 0;

	dev_dbg(tty->dev, "%s - set=%d, clear=%d\n", __func__, set, clear);

	spin_lock_irqsave(&metro_priv->lock, flags);
	control_state = metro_priv->control_state;

	/* Set the RTS and DTR values. */
	if (set & TIOCM_RTS)
		control_state |= TIOCM_RTS;
	if (set & TIOCM_DTR)
		control_state |= TIOCM_DTR;
	if (clear & TIOCM_RTS)
		control_state &= ~TIOCM_RTS;
	if (clear & TIOCM_DTR)
		control_state &= ~TIOCM_DTR;

	metro_priv->control_state = control_state;
	spin_unlock_irqrestore(&metro_priv->lock, flags);
	return metrousb_set_modem_ctrl(serial, control_state);
}

static void metrousb_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct metrousb_private *metro_priv = usb_get_serial_port_data(port);
	unsigned long flags = 0;
	int result = 0;

	dev_dbg(tty->dev, "%s\n", __func__);

	/* Set the private information for the port to resume reading data. */
	spin_lock_irqsave(&metro_priv->lock, flags);
	metro_priv->throttled = 0;
	spin_unlock_irqrestore(&metro_priv->lock, flags);

	/* Submit the urb to read from the port. */
	port->interrupt_in_urb->dev = port->serial->dev;
	result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
	if (result)
		dev_err(tty->dev,
			"failed submitting interrupt in urb error code=%d\n",
			result);
}

static struct usb_serial_driver metrousb_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"metro-usb",
	},
	.description		= "Metrologic USB to Serial",
	.id_table		= id_table,
	.num_ports		= 1,
	.open			= metrousb_open,
	.close			= metrousb_cleanup,
	.read_int_callback	= metrousb_read_int_callback,
	.write_int_callback	= metrousb_write_int_callback,
	.port_probe		= metrousb_port_probe,
	.port_remove		= metrousb_port_remove,
	.throttle		= metrousb_throttle,
	.unthrottle		= metrousb_unthrottle,
	.tiocmget		= metrousb_tiocmget,
	.tiocmset		= metrousb_tiocmset,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&metrousb_device,
	NULL,
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Philip Nicastro");
MODULE_AUTHOR("Aleksey Babahin <tamerlan311@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
