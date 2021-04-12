// SPDX-License-Identifier: GPL-2.0+
/*
 * MaxLinear/Exar USB to Serial driver
 *
 * Copyright (c) 2020 Manivannan Sadhasivam <mani@kernel.org>
 *
 * Based on the initial driver written by Patong Yang:
 *
 *   https://lore.kernel.org/r/20180404070634.nhspvmxcjwfgjkcv@advantechmxl-desktop
 *
 *   Copyright (c) 2018 Patong Yang <patong.mxl@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/serial.h>

struct xr_txrx_clk_mask {
	u16 tx;
	u16 rx0;
	u16 rx1;
};

#define XR_INT_OSC_HZ			48000000U
#define XR21V141X_MIN_SPEED		46U
#define XR21V141X_MAX_SPEED		XR_INT_OSC_HZ

/* USB requests */
#define XR21V141X_SET_REQ		0
#define XR21V141X_GET_REQ		1

/* XR21V141X register blocks */
#define XR21V141X_UART_REG_BLOCK	0
#define XR21V141X_UM_REG_BLOCK		4
#define XR21V141X_UART_CUSTOM_BLOCK	0x66

/* XR21V141X UART registers */
#define XR21V141X_CLOCK_DIVISOR_0	0x04
#define XR21V141X_CLOCK_DIVISOR_1	0x05
#define XR21V141X_CLOCK_DIVISOR_2	0x06
#define XR21V141X_TX_CLOCK_MASK_0	0x07
#define XR21V141X_TX_CLOCK_MASK_1	0x08
#define XR21V141X_RX_CLOCK_MASK_0	0x09
#define XR21V141X_RX_CLOCK_MASK_1	0x0a
#define XR21V141X_REG_FORMAT		0x0b

/* XR21V141X UART Manager registers */
#define XR21V141X_UM_FIFO_ENABLE_REG	0x10
#define XR21V141X_UM_ENABLE_TX_FIFO	0x01
#define XR21V141X_UM_ENABLE_RX_FIFO	0x02

#define XR21V141X_UM_RX_FIFO_RESET	0x18
#define XR21V141X_UM_TX_FIFO_RESET	0x1c

#define XR_UART_ENABLE_TX		0x1
#define XR_UART_ENABLE_RX		0x2

#define XR_GPIO_RI			BIT(0)
#define XR_GPIO_CD			BIT(1)
#define XR_GPIO_DSR			BIT(2)
#define XR_GPIO_DTR			BIT(3)
#define XR_GPIO_CTS			BIT(4)
#define XR_GPIO_RTS			BIT(5)

#define XR21V141X_UART_BREAK_ON		0xff
#define XR21V141X_UART_BREAK_OFF	0

#define XR_UART_DATA_MASK		GENMASK(3, 0)
#define XR_UART_DATA_7			0x7
#define XR_UART_DATA_8			0x8

#define XR_UART_PARITY_MASK		GENMASK(6, 4)
#define XR_UART_PARITY_SHIFT		4
#define XR_UART_PARITY_NONE		(0x0 << XR_UART_PARITY_SHIFT)
#define XR_UART_PARITY_ODD		(0x1 << XR_UART_PARITY_SHIFT)
#define XR_UART_PARITY_EVEN		(0x2 <<	XR_UART_PARITY_SHIFT)
#define XR_UART_PARITY_MARK		(0x3 << XR_UART_PARITY_SHIFT)
#define XR_UART_PARITY_SPACE		(0x4 << XR_UART_PARITY_SHIFT)

#define XR_UART_STOP_MASK		BIT(7)
#define XR_UART_STOP_SHIFT		7
#define XR_UART_STOP_1			(0x0 << XR_UART_STOP_SHIFT)
#define XR_UART_STOP_2			(0x1 << XR_UART_STOP_SHIFT)

#define XR_UART_FLOW_MODE_NONE		0x0
#define XR_UART_FLOW_MODE_HW		0x1
#define XR_UART_FLOW_MODE_SW		0x2

#define XR_GPIO_MODE_MASK		GENMASK(2, 0)
#define XR_GPIO_MODE_RTS_CTS		0x1
#define XR_GPIO_MODE_DTR_DSR		0x2
#define XR_GPIO_MODE_RS485		0x3
#define XR_GPIO_MODE_RS485_ADDR		0x4

struct xr_type {
	u8 uart_enable;
	u8 flow_control;
	u8 xon_char;
	u8 xoff_char;
	u8 tx_break;
	u8 gpio_mode;
	u8 gpio_direction;
	u8 gpio_set;
	u8 gpio_clear;
	u8 gpio_status;
};

enum xr_type_id {
	XR21V141X,
	XR_TYPE_COUNT,
};

static const struct xr_type xr_types[] = {
	[XR21V141X] = {
		.uart_enable	= 0x03,
		.flow_control	= 0x0c,
		.xon_char	= 0x10,
		.xoff_char	= 0x11,
		.tx_break	= 0x14,
		.gpio_mode	= 0x1a,
		.gpio_direction	= 0x1b,
		.gpio_set	= 0x1d,
		.gpio_clear	= 0x1e,
		.gpio_status	= 0x1f,
	},
};

struct xr_data {
	const struct xr_type *type;
	u8 channel;			/* zero-based index */
};

static int xr_set_reg(struct usb_serial_port *port, u8 block, u8 reg, u8 val)
{
	struct usb_serial *serial = port->serial;
	int ret;

	ret = usb_control_msg(serial->dev,
			      usb_sndctrlpipe(serial->dev, 0),
			      XR21V141X_SET_REQ,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      val, reg | (block << 8), NULL, 0,
			      USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&port->dev, "Failed to set reg 0x%02x: %d\n", reg, ret);
		return ret;
	}

	return 0;
}

static int xr_get_reg(struct usb_serial_port *port, u8 block, u8 reg, u8 *val)
{
	struct usb_serial *serial = port->serial;
	u8 *dmabuf;
	int ret;

	dmabuf = kmalloc(1, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	ret = usb_control_msg(serial->dev,
			      usb_rcvctrlpipe(serial->dev, 0),
			      XR21V141X_GET_REQ,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0, reg | (block << 8), dmabuf, 1,
			      USB_CTRL_GET_TIMEOUT);
	if (ret == 1) {
		*val = *dmabuf;
		ret = 0;
	} else {
		dev_err(&port->dev, "Failed to get reg 0x%02x: %d\n", reg, ret);
		if (ret >= 0)
			ret = -EIO;
	}

	kfree(dmabuf);

	return ret;
}

static int xr_set_reg_uart(struct usb_serial_port *port, u8 reg, u8 val)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	u8 block;

	block = XR21V141X_UART_REG_BLOCK + data->channel;

	return xr_set_reg(port, block, reg, val);
}

static int xr_get_reg_uart(struct usb_serial_port *port, u8 reg, u8 *val)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	u8 block;

	block = XR21V141X_UART_REG_BLOCK + data->channel;

	return xr_get_reg(port, block, reg, val);
}

static int xr_set_reg_um(struct usb_serial_port *port, u8 reg_base, u8 val)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	u8 reg;

	reg = reg_base + data->channel;

	return xr_set_reg(port, XR21V141X_UM_REG_BLOCK, reg, val);
}

/*
 * According to datasheet, below is the recommended sequence for enabling UART
 * module in XR21V141X:
 *
 * Enable Tx FIFO
 * Enable Tx and Rx
 * Enable Rx FIFO
 */
static int xr_uart_enable(struct usb_serial_port *port)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	int ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_FIFO_ENABLE_REG,
			    XR21V141X_UM_ENABLE_TX_FIFO);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, data->type->uart_enable,
			      XR_UART_ENABLE_TX | XR_UART_ENABLE_RX);
	if (ret)
		return ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_FIFO_ENABLE_REG,
			    XR21V141X_UM_ENABLE_TX_FIFO | XR21V141X_UM_ENABLE_RX_FIFO);
	if (ret)
		xr_set_reg_uart(port, data->type->uart_enable, 0);

	return ret;
}

static int xr_uart_disable(struct usb_serial_port *port)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	int ret;

	ret = xr_set_reg_uart(port, data->type->uart_enable, 0);
	if (ret)
		return ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_FIFO_ENABLE_REG, 0);

	return ret;
}

static int xr_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct xr_data *data = usb_get_serial_port_data(port);
	u8 status;
	int ret;

	ret = xr_get_reg_uart(port, data->type->gpio_status, &status);
	if (ret)
		return ret;

	/*
	 * Modem control pins are active low, so reading '0' means it is active
	 * and '1' means not active.
	 */
	ret = ((status & XR_GPIO_DTR) ? 0 : TIOCM_DTR) |
	      ((status & XR_GPIO_RTS) ? 0 : TIOCM_RTS) |
	      ((status & XR_GPIO_CTS) ? 0 : TIOCM_CTS) |
	      ((status & XR_GPIO_DSR) ? 0 : TIOCM_DSR) |
	      ((status & XR_GPIO_RI) ? 0 : TIOCM_RI) |
	      ((status & XR_GPIO_CD) ? 0 : TIOCM_CD);

	return ret;
}

static int xr_tiocmset_port(struct usb_serial_port *port,
			    unsigned int set, unsigned int clear)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	const struct xr_type *type = data->type;
	u8 gpio_set = 0;
	u8 gpio_clr = 0;
	int ret = 0;

	/* Modem control pins are active low, so set & clr are swapped */
	if (set & TIOCM_RTS)
		gpio_clr |= XR_GPIO_RTS;
	if (set & TIOCM_DTR)
		gpio_clr |= XR_GPIO_DTR;
	if (clear & TIOCM_RTS)
		gpio_set |= XR_GPIO_RTS;
	if (clear & TIOCM_DTR)
		gpio_set |= XR_GPIO_DTR;

	/* Writing '0' to gpio_{set/clr} bits has no effect, so no need to do */
	if (gpio_clr)
		ret = xr_set_reg_uart(port, type->gpio_clear, gpio_clr);

	if (gpio_set)
		ret = xr_set_reg_uart(port, type->gpio_set, gpio_set);

	return ret;
}

static int xr_tiocmset(struct tty_struct *tty,
		       unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;

	return xr_tiocmset_port(port, set, clear);
}

static void xr_dtr_rts(struct usb_serial_port *port, int on)
{
	if (on)
		xr_tiocmset_port(port, TIOCM_DTR | TIOCM_RTS, 0);
	else
		xr_tiocmset_port(port, 0, TIOCM_DTR | TIOCM_RTS);
}

static void xr_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct xr_data *data = usb_get_serial_port_data(port);
	const struct xr_type *type = data->type;
	u8 state;

	if (break_state == 0)
		state = XR21V141X_UART_BREAK_OFF;
	else
		state = XR21V141X_UART_BREAK_ON;

	dev_dbg(&port->dev, "Turning break %s\n",
		state == XR21V141X_UART_BREAK_OFF ? "off" : "on");
	xr_set_reg_uart(port, type->tx_break, state);
}

/* Tx and Rx clock mask values obtained from section 3.3.4 of datasheet */
static const struct xr_txrx_clk_mask xr21v141x_txrx_clk_masks[] = {
	{ 0x000, 0x000, 0x000 },
	{ 0x000, 0x000, 0x000 },
	{ 0x100, 0x000, 0x100 },
	{ 0x020, 0x400, 0x020 },
	{ 0x010, 0x100, 0x010 },
	{ 0x208, 0x040, 0x208 },
	{ 0x104, 0x820, 0x108 },
	{ 0x844, 0x210, 0x884 },
	{ 0x444, 0x110, 0x444 },
	{ 0x122, 0x888, 0x224 },
	{ 0x912, 0x448, 0x924 },
	{ 0x492, 0x248, 0x492 },
	{ 0x252, 0x928, 0x292 },
	{ 0x94a, 0x4a4, 0xa52 },
	{ 0x52a, 0xaa4, 0x54a },
	{ 0xaaa, 0x954, 0x4aa },
	{ 0xaaa, 0x554, 0xaaa },
	{ 0x555, 0xad4, 0x5aa },
	{ 0xb55, 0xab4, 0x55a },
	{ 0x6b5, 0x5ac, 0xb56 },
	{ 0x5b5, 0xd6c, 0x6d6 },
	{ 0xb6d, 0xb6a, 0xdb6 },
	{ 0x76d, 0x6da, 0xbb6 },
	{ 0xedd, 0xdda, 0x76e },
	{ 0xddd, 0xbba, 0xeee },
	{ 0x7bb, 0xf7a, 0xdde },
	{ 0xf7b, 0xef6, 0x7de },
	{ 0xdf7, 0xbf6, 0xf7e },
	{ 0x7f7, 0xfee, 0xefe },
	{ 0xfdf, 0xfbe, 0x7fe },
	{ 0xf7f, 0xefe, 0xffe },
	{ 0xfff, 0xffe, 0xffd },
};

static int xr_set_baudrate(struct tty_struct *tty,
			   struct usb_serial_port *port)
{
	u32 divisor, baud, idx;
	u16 tx_mask, rx_mask;
	int ret;

	baud = tty->termios.c_ospeed;
	if (!baud)
		return 0;

	baud = clamp(baud, XR21V141X_MIN_SPEED, XR21V141X_MAX_SPEED);
	divisor = XR_INT_OSC_HZ / baud;
	idx = ((32 * XR_INT_OSC_HZ) / baud) & 0x1f;
	tx_mask = xr21v141x_txrx_clk_masks[idx].tx;

	if (divisor & 0x01)
		rx_mask = xr21v141x_txrx_clk_masks[idx].rx1;
	else
		rx_mask = xr21v141x_txrx_clk_masks[idx].rx0;

	dev_dbg(&port->dev, "Setting baud rate: %u\n", baud);
	/*
	 * XR21V141X uses fractional baud rate generator with 48MHz internal
	 * oscillator and 19-bit programmable divisor. So theoretically it can
	 * generate most commonly used baud rates with high accuracy.
	 */
	ret = xr_set_reg_uart(port, XR21V141X_CLOCK_DIVISOR_0,
			      divisor & 0xff);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, XR21V141X_CLOCK_DIVISOR_1,
			      (divisor >>  8) & 0xff);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, XR21V141X_CLOCK_DIVISOR_2,
			      (divisor >> 16) & 0xff);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, XR21V141X_TX_CLOCK_MASK_0,
			      tx_mask & 0xff);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, XR21V141X_TX_CLOCK_MASK_1,
			      (tx_mask >>  8) & 0xff);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, XR21V141X_RX_CLOCK_MASK_0,
			      rx_mask & 0xff);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, XR21V141X_RX_CLOCK_MASK_1,
			      (rx_mask >>  8) & 0xff);
	if (ret)
		return ret;

	tty_encode_baud_rate(tty, baud, baud);

	return 0;
}

static void xr_set_flow_mode(struct tty_struct *tty,
			     struct usb_serial_port *port,
			     struct ktermios *old_termios)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	const struct xr_type *type = data->type;
	u8 flow, gpio_mode;
	int ret;

	ret = xr_get_reg_uart(port, type->gpio_mode, &gpio_mode);
	if (ret)
		return;

	/* Set GPIO mode for controlling the pins manually by default. */
	gpio_mode &= ~XR_GPIO_MODE_MASK;

	if (C_CRTSCTS(tty) && C_BAUD(tty) != B0) {
		dev_dbg(&port->dev, "Enabling hardware flow ctrl\n");
		gpio_mode |= XR_GPIO_MODE_RTS_CTS;
		flow = XR_UART_FLOW_MODE_HW;
	} else if (I_IXON(tty)) {
		u8 start_char = START_CHAR(tty);
		u8 stop_char = STOP_CHAR(tty);

		dev_dbg(&port->dev, "Enabling sw flow ctrl\n");
		flow = XR_UART_FLOW_MODE_SW;

		xr_set_reg_uart(port, type->xon_char, start_char);
		xr_set_reg_uart(port, type->xoff_char, stop_char);
	} else {
		dev_dbg(&port->dev, "Disabling flow ctrl\n");
		flow = XR_UART_FLOW_MODE_NONE;
	}

	/*
	 * As per the datasheet, UART needs to be disabled while writing to
	 * FLOW_CONTROL register.
	 */
	xr_uart_disable(port);
	xr_set_reg_uart(port, type->flow_control, flow);
	xr_uart_enable(port);

	xr_set_reg_uart(port, type->gpio_mode, gpio_mode);

	if (C_BAUD(tty) == B0)
		xr_dtr_rts(port, 0);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		xr_dtr_rts(port, 1);
}

static void xr_set_termios(struct tty_struct *tty,
			   struct usb_serial_port *port,
			   struct ktermios *old_termios)
{
	struct ktermios *termios = &tty->termios;
	u8 bits = 0;
	int ret;

	if (!old_termios || (tty->termios.c_ospeed != old_termios->c_ospeed))
		xr_set_baudrate(tty, port);

	switch (C_CSIZE(tty)) {
	case CS5:
	case CS6:
		/* CS5 and CS6 are not supported, so just restore old setting */
		termios->c_cflag &= ~CSIZE;
		if (old_termios)
			termios->c_cflag |= old_termios->c_cflag & CSIZE;
		else
			termios->c_cflag |= CS8;

		if (C_CSIZE(tty) == CS7)
			bits |= XR_UART_DATA_7;
		else
			bits |= XR_UART_DATA_8;
		break;
	case CS7:
		bits |= XR_UART_DATA_7;
		break;
	case CS8:
	default:
		bits |= XR_UART_DATA_8;
		break;
	}

	if (C_PARENB(tty)) {
		if (C_CMSPAR(tty)) {
			if (C_PARODD(tty))
				bits |= XR_UART_PARITY_MARK;
			else
				bits |= XR_UART_PARITY_SPACE;
		} else {
			if (C_PARODD(tty))
				bits |= XR_UART_PARITY_ODD;
			else
				bits |= XR_UART_PARITY_EVEN;
		}
	}

	if (C_CSTOPB(tty))
		bits |= XR_UART_STOP_2;
	else
		bits |= XR_UART_STOP_1;

	ret = xr_set_reg_uart(port, XR21V141X_REG_FORMAT, bits);
	if (ret)
		return;

	xr_set_flow_mode(tty, port, old_termios);
}

static int xr_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int ret;

	ret = xr_uart_enable(port);
	if (ret) {
		dev_err(&port->dev, "Failed to enable UART\n");
		return ret;
	}

	/* Setup termios */
	if (tty)
		xr_set_termios(tty, port, NULL);

	ret = usb_serial_generic_open(tty, port);
	if (ret) {
		xr_uart_disable(port);
		return ret;
	}

	return 0;
}

static void xr_close(struct usb_serial_port *port)
{
	usb_serial_generic_close(port);

	xr_uart_disable(port);
}

static int xr_probe(struct usb_serial *serial, const struct usb_device_id *id)
{
	struct usb_interface *control = serial->interface;
	struct usb_host_interface *alt = control->cur_altsetting;
	struct usb_cdc_parsed_header hdrs;
	struct usb_cdc_union_desc *desc;
	struct usb_interface *data;
	int ret;

	ret = cdc_parse_cdc_header(&hdrs, control, alt->extra, alt->extralen);
	if (ret < 0)
		return -ENODEV;

	desc = hdrs.usb_cdc_union_desc;
	if (!desc)
		return -ENODEV;

	data = usb_ifnum_to_if(serial->dev, desc->bSlaveInterface0);
	if (!data)
		return -ENODEV;

	ret = usb_serial_claim_interface(serial, data);
	if (ret)
		return ret;

	usb_set_serial_data(serial, (void *)id->driver_info);

	return 0;
}

static int xr_gpio_init(struct usb_serial_port *port, const struct xr_type *type)
{
	u8 mask, mode;
	int ret;

	/* Configure all pins as GPIO. */
	mode = 0;
	ret = xr_set_reg_uart(port, type->gpio_mode, mode);
	if (ret)
		return ret;

	/*
	 * Configure DTR and RTS as outputs and make sure they are deasserted
	 * (active low), and configure RI, CD, DSR and CTS as inputs.
	 */
	mask = XR_GPIO_DTR | XR_GPIO_RTS;
	ret = xr_set_reg_uart(port, type->gpio_direction, mask);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, type->gpio_set, mask);
	if (ret)
		return ret;

	return 0;
}

static int xr_port_probe(struct usb_serial_port *port)
{
	struct usb_interface_descriptor *desc;
	const struct xr_type *type;
	struct xr_data *data;
	enum xr_type_id type_id;
	int ret;

	type_id = (int)(unsigned long)usb_get_serial_data(port->serial);
	type = &xr_types[type_id];

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->type = type;

	desc = &port->serial->interface->cur_altsetting->desc;
	data->channel = desc->bInterfaceNumber / 2;

	usb_set_serial_port_data(port, data);

	ret = xr_gpio_init(port, type);
	if (ret)
		goto err_free;

	return 0;

err_free:
	kfree(data);

	return ret;
}

static void xr_port_remove(struct usb_serial_port *port)
{
	struct xr_data *data = usb_get_serial_port_data(port);

	kfree(data);
}

#define XR_DEVICE(vid, pid, type)					\
	USB_DEVICE_INTERFACE_CLASS((vid), (pid), USB_CLASS_COMM),	\
	.driver_info = (type)

static const struct usb_device_id id_table[] = {
	{ XR_DEVICE(0x04e2, 0x1410, XR21V141X) },
	{ XR_DEVICE(0x04e2, 0x1412, XR21V141X) },
	{ XR_DEVICE(0x04e2, 0x1414, XR21V141X) },
	{ }
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver xr_device = {
	.driver = {
		.owner = THIS_MODULE,
		.name =	"xr_serial",
	},
	.id_table		= id_table,
	.num_ports		= 1,
	.probe			= xr_probe,
	.port_probe		= xr_port_probe,
	.port_remove		= xr_port_remove,
	.open			= xr_open,
	.close			= xr_close,
	.break_ctl		= xr_break_ctl,
	.set_termios		= xr_set_termios,
	.tiocmget		= xr_tiocmget,
	.tiocmset		= xr_tiocmset,
	.dtr_rts		= xr_dtr_rts
};

static struct usb_serial_driver * const serial_drivers[] = {
	&xr_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR("Manivannan Sadhasivam <mani@kernel.org>");
MODULE_DESCRIPTION("MaxLinear/Exar USB to Serial driver");
MODULE_LICENSE("GPL");
