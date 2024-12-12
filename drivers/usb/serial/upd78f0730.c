// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas Electronics uPD78F0730 USB to serial converter driver
 *
 * Copyright (C) 2014,2016 Maksim Salau <maksim.salau@gmail.com>
 *
 * Protocol of the adaptor is described in the application note U19660EJ1V0AN00
 * μPD78F0730 8-bit Single-Chip Microcontroller
 * USB-to-Serial Conversion Software
 * <https://www.renesas.com/en-eu/doc/DocumentServer/026/U19660EJ1V0AN00.pdf>
 *
 * The adaptor functionality is limited to the following:
 * - data bits: 7 or 8
 * - stop bits: 1 or 2
 * - parity: even, odd or none
 * - flow control: none
 * - baud rates: 0, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 153600
 * - signals: DTR, RTS and BREAK
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define DRIVER_DESC "Renesas uPD78F0730 USB to serial converter driver"

#define DRIVER_AUTHOR "Maksim Salau <maksim.salau@gmail.com>"

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x0409, 0x0063) }, /* V850ESJX3-STICK */
	{ USB_DEVICE(0x045B, 0x0212) }, /* YRPBRL78G13, YRPBRL78G14 */
	{ USB_DEVICE(0x064B, 0x7825) }, /* Analog Devices EVAL-ADXL362Z-DB */
	{}
};

MODULE_DEVICE_TABLE(usb, id_table);

/*
 * Each adaptor is associated with a private structure, that holds the current
 * state of control signals (DTR, RTS and BREAK).
 */
struct upd78f0730_port_private {
	struct mutex	lock;		/* mutex to protect line_signals */
	u8		line_signals;
};

/* Op-codes of control commands */
#define UPD78F0730_CMD_LINE_CONTROL	0x00
#define UPD78F0730_CMD_SET_DTR_RTS	0x01
#define UPD78F0730_CMD_SET_XON_XOFF_CHR	0x02
#define UPD78F0730_CMD_OPEN_CLOSE	0x03
#define UPD78F0730_CMD_SET_ERR_CHR	0x04

/* Data sizes in UPD78F0730_CMD_LINE_CONTROL command */
#define UPD78F0730_DATA_SIZE_7_BITS	0x00
#define UPD78F0730_DATA_SIZE_8_BITS	0x01
#define UPD78F0730_DATA_SIZE_MASK	0x01

/* Stop-bit modes in UPD78F0730_CMD_LINE_CONTROL command */
#define UPD78F0730_STOP_BIT_1_BIT	0x00
#define UPD78F0730_STOP_BIT_2_BIT	0x02
#define UPD78F0730_STOP_BIT_MASK	0x02

/* Parity modes in UPD78F0730_CMD_LINE_CONTROL command */
#define UPD78F0730_PARITY_NONE	0x00
#define UPD78F0730_PARITY_EVEN	0x04
#define UPD78F0730_PARITY_ODD	0x08
#define UPD78F0730_PARITY_MASK	0x0C

/* Flow control modes in UPD78F0730_CMD_LINE_CONTROL command */
#define UPD78F0730_FLOW_CONTROL_NONE	0x00
#define UPD78F0730_FLOW_CONTROL_HW	0x10
#define UPD78F0730_FLOW_CONTROL_SW	0x20
#define UPD78F0730_FLOW_CONTROL_MASK	0x30

/* Control signal bits in UPD78F0730_CMD_SET_DTR_RTS command */
#define UPD78F0730_RTS		0x01
#define UPD78F0730_DTR		0x02
#define UPD78F0730_BREAK	0x04

/* Port modes in UPD78F0730_CMD_OPEN_CLOSE command */
#define UPD78F0730_PORT_CLOSE	0x00
#define UPD78F0730_PORT_OPEN	0x01

/* Error character substitution modes in UPD78F0730_CMD_SET_ERR_CHR command */
#define UPD78F0730_ERR_CHR_DISABLED	0x00
#define UPD78F0730_ERR_CHR_ENABLED	0x01

/*
 * Declaration of command structures
 */

/* UPD78F0730_CMD_LINE_CONTROL command */
struct upd78f0730_line_control {
	u8	opcode;
	__le32	baud_rate;
	u8	params;
} __packed;

/* UPD78F0730_CMD_SET_DTR_RTS command */
struct upd78f0730_set_dtr_rts {
	u8 opcode;
	u8 params;
};

/* UPD78F0730_CMD_SET_XON_OFF_CHR command */
struct upd78f0730_set_xon_xoff_chr {
	u8 opcode;
	u8 xon;
	u8 xoff;
};

/* UPD78F0730_CMD_OPEN_CLOSE command */
struct upd78f0730_open_close {
	u8 opcode;
	u8 state;
};

/* UPD78F0730_CMD_SET_ERR_CHR command */
struct upd78f0730_set_err_chr {
	u8 opcode;
	u8 state;
	u8 err_char;
};

static int upd78f0730_send_ctl(struct usb_serial_port *port,
			const void *data, int size)
{
	struct usb_device *usbdev = port->serial->dev;
	void *buf;
	int res;

	if (size <= 0 || !data)
		return -EINVAL;

	buf = kmemdup(data, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	res = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev, 0), 0x00,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			0x0000, 0x0000, buf, size, USB_CTRL_SET_TIMEOUT);

	kfree(buf);

	if (res < 0) {
		struct device *dev = &port->dev;

		dev_err(dev, "failed to send control request %02x: %d\n",
			*(u8 *)data, res);

		return res;
	}

	return 0;
}

static int upd78f0730_port_probe(struct usb_serial_port *port)
{
	struct upd78f0730_port_private *private;

	private = kzalloc(sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	mutex_init(&private->lock);
	usb_set_serial_port_data(port, private);

	return 0;
}

static void upd78f0730_port_remove(struct usb_serial_port *port)
{
	struct upd78f0730_port_private *private;

	private = usb_get_serial_port_data(port);
	mutex_destroy(&private->lock);
	kfree(private);
}

static int upd78f0730_tiocmget(struct tty_struct *tty)
{
	struct upd78f0730_port_private *private;
	struct usb_serial_port *port = tty->driver_data;
	int signals;
	int res;

	private = usb_get_serial_port_data(port);

	mutex_lock(&private->lock);
	signals = private->line_signals;
	mutex_unlock(&private->lock);

	res = ((signals & UPD78F0730_DTR) ? TIOCM_DTR : 0) |
		((signals & UPD78F0730_RTS) ? TIOCM_RTS : 0);

	dev_dbg(&port->dev, "%s - res = %x\n", __func__, res);

	return res;
}

static int upd78f0730_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct upd78f0730_port_private *private;
	struct upd78f0730_set_dtr_rts request;
	struct device *dev = &port->dev;
	int res;

	private = usb_get_serial_port_data(port);

	mutex_lock(&private->lock);
	if (set & TIOCM_DTR) {
		private->line_signals |= UPD78F0730_DTR;
		dev_dbg(dev, "%s - set DTR\n", __func__);
	}
	if (set & TIOCM_RTS) {
		private->line_signals |= UPD78F0730_RTS;
		dev_dbg(dev, "%s - set RTS\n", __func__);
	}
	if (clear & TIOCM_DTR) {
		private->line_signals &= ~UPD78F0730_DTR;
		dev_dbg(dev, "%s - clear DTR\n", __func__);
	}
	if (clear & TIOCM_RTS) {
		private->line_signals &= ~UPD78F0730_RTS;
		dev_dbg(dev, "%s - clear RTS\n", __func__);
	}
	request.opcode = UPD78F0730_CMD_SET_DTR_RTS;
	request.params = private->line_signals;

	res = upd78f0730_send_ctl(port, &request, sizeof(request));
	mutex_unlock(&private->lock);

	return res;
}

static int upd78f0730_break_ctl(struct tty_struct *tty, int break_state)
{
	struct upd78f0730_port_private *private;
	struct usb_serial_port *port = tty->driver_data;
	struct upd78f0730_set_dtr_rts request;
	struct device *dev = &port->dev;
	int res;

	private = usb_get_serial_port_data(port);

	mutex_lock(&private->lock);
	if (break_state) {
		private->line_signals |= UPD78F0730_BREAK;
		dev_dbg(dev, "%s - set BREAK\n", __func__);
	} else {
		private->line_signals &= ~UPD78F0730_BREAK;
		dev_dbg(dev, "%s - clear BREAK\n", __func__);
	}
	request.opcode = UPD78F0730_CMD_SET_DTR_RTS;
	request.params = private->line_signals;

	res = upd78f0730_send_ctl(port, &request, sizeof(request));
	mutex_unlock(&private->lock);

	return res;
}

static void upd78f0730_dtr_rts(struct usb_serial_port *port, int on)
{
	struct tty_struct *tty = port->port.tty;
	unsigned int set = 0;
	unsigned int clear = 0;

	if (on)
		set = TIOCM_DTR | TIOCM_RTS;
	else
		clear = TIOCM_DTR | TIOCM_RTS;

	upd78f0730_tiocmset(tty, set, clear);
}

static speed_t upd78f0730_get_baud_rate(struct tty_struct *tty)
{
	const speed_t baud_rate = tty_get_baud_rate(tty);
	static const speed_t supported[] = {
		0, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 153600
	};
	int i;

	for (i = ARRAY_SIZE(supported) - 1; i >= 0; i--) {
		if (baud_rate == supported[i])
			return baud_rate;
	}

	/* If the baud rate is not supported, switch to the default one */
	tty_encode_baud_rate(tty, 9600, 9600);

	return tty_get_baud_rate(tty);
}

static void upd78f0730_set_termios(struct tty_struct *tty,
				   struct usb_serial_port *port,
				   const struct ktermios *old_termios)
{
	struct device *dev = &port->dev;
	struct upd78f0730_line_control request;
	speed_t baud_rate;

	if (old_termios && !tty_termios_hw_change(&tty->termios, old_termios))
		return;

	if (C_BAUD(tty) == B0)
		upd78f0730_dtr_rts(port, 0);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		upd78f0730_dtr_rts(port, 1);

	baud_rate = upd78f0730_get_baud_rate(tty);
	request.opcode = UPD78F0730_CMD_LINE_CONTROL;
	request.baud_rate = cpu_to_le32(baud_rate);
	request.params = 0;
	dev_dbg(dev, "%s - baud rate = %d\n", __func__, baud_rate);

	switch (C_CSIZE(tty)) {
	case CS7:
		request.params |= UPD78F0730_DATA_SIZE_7_BITS;
		dev_dbg(dev, "%s - 7 data bits\n", __func__);
		break;
	default:
		tty->termios.c_cflag &= ~CSIZE;
		tty->termios.c_cflag |= CS8;
		dev_warn(dev, "data size is not supported, using 8 bits\n");
		fallthrough;
	case CS8:
		request.params |= UPD78F0730_DATA_SIZE_8_BITS;
		dev_dbg(dev, "%s - 8 data bits\n", __func__);
		break;
	}

	if (C_PARENB(tty)) {
		if (C_PARODD(tty)) {
			request.params |= UPD78F0730_PARITY_ODD;
			dev_dbg(dev, "%s - odd parity\n", __func__);
		} else {
			request.params |= UPD78F0730_PARITY_EVEN;
			dev_dbg(dev, "%s - even parity\n", __func__);
		}

		if (C_CMSPAR(tty)) {
			tty->termios.c_cflag &= ~CMSPAR;
			dev_warn(dev, "MARK/SPACE parity is not supported\n");
		}
	} else {
		request.params |= UPD78F0730_PARITY_NONE;
		dev_dbg(dev, "%s - no parity\n", __func__);
	}

	if (C_CSTOPB(tty)) {
		request.params |= UPD78F0730_STOP_BIT_2_BIT;
		dev_dbg(dev, "%s - 2 stop bits\n", __func__);
	} else {
		request.params |= UPD78F0730_STOP_BIT_1_BIT;
		dev_dbg(dev, "%s - 1 stop bit\n", __func__);
	}

	if (C_CRTSCTS(tty)) {
		tty->termios.c_cflag &= ~CRTSCTS;
		dev_warn(dev, "RTSCTS flow control is not supported\n");
	}
	if (I_IXOFF(tty) || I_IXON(tty)) {
		tty->termios.c_iflag &= ~(IXOFF | IXON);
		dev_warn(dev, "XON/XOFF flow control is not supported\n");
	}
	request.params |= UPD78F0730_FLOW_CONTROL_NONE;
	dev_dbg(dev, "%s - no flow control\n", __func__);

	upd78f0730_send_ctl(port, &request, sizeof(request));
}

static int upd78f0730_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	static const struct upd78f0730_open_close request = {
		.opcode = UPD78F0730_CMD_OPEN_CLOSE,
		.state = UPD78F0730_PORT_OPEN
	};
	int res;

	res = upd78f0730_send_ctl(port, &request, sizeof(request));
	if (res)
		return res;

	if (tty)
		upd78f0730_set_termios(tty, port, NULL);

	return usb_serial_generic_open(tty, port);
}

static void upd78f0730_close(struct usb_serial_port *port)
{
	static const struct upd78f0730_open_close request = {
		.opcode = UPD78F0730_CMD_OPEN_CLOSE,
		.state = UPD78F0730_PORT_CLOSE
	};

	usb_serial_generic_close(port);
	upd78f0730_send_ctl(port, &request, sizeof(request));
}

static struct usb_serial_driver upd78f0730_device = {
	.driver	 = {
		.name	= "upd78f0730",
	},
	.id_table	= id_table,
	.num_ports	= 1,
	.port_probe	= upd78f0730_port_probe,
	.port_remove	= upd78f0730_port_remove,
	.open		= upd78f0730_open,
	.close		= upd78f0730_close,
	.set_termios	= upd78f0730_set_termios,
	.tiocmget	= upd78f0730_tiocmget,
	.tiocmset	= upd78f0730_tiocmset,
	.dtr_rts	= upd78f0730_dtr_rts,
	.break_ctl	= upd78f0730_break_ctl,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&upd78f0730_device,
	NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL v2");
