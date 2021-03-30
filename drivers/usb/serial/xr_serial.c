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
#include <linux/usb/serial.h>

struct xr_txrx_clk_mask {
	u16 tx;
	u16 rx0;
	u16 rx1;
};

#define XR_INT_OSC_HZ			48000000U
#define XR21V141X_MIN_SPEED		46U
#define XR21V141X_MAX_SPEED		XR_INT_OSC_HZ

/* USB Requests */
#define XR21V141X_SET_REQ		0
#define XR21V141X_GET_REQ		1

#define XR21V141X_CLOCK_DIVISOR_0	0x04
#define XR21V141X_CLOCK_DIVISOR_1	0x05
#define XR21V141X_CLOCK_DIVISOR_2	0x06
#define XR21V141X_TX_CLOCK_MASK_0	0x07
#define XR21V141X_TX_CLOCK_MASK_1	0x08
#define XR21V141X_RX_CLOCK_MASK_0	0x09
#define XR21V141X_RX_CLOCK_MASK_1	0x0a

/* XR21V141X register blocks */
#define XR21V141X_UART_REG_BLOCK	0
#define XR21V141X_UM_REG_BLOCK		4
#define XR21V141X_UART_CUSTOM_BLOCK	0x66

/* XR21V141X UART Manager Registers */
#define XR21V141X_UM_FIFO_ENABLE_REG	0x10
#define XR21V141X_UM_ENABLE_TX_FIFO	0x01
#define XR21V141X_UM_ENABLE_RX_FIFO	0x02

#define XR21V141X_UM_RX_FIFO_RESET	0x18
#define XR21V141X_UM_TX_FIFO_RESET	0x1c

#define XR21V141X_UART_ENABLE_TX	0x1
#define XR21V141X_UART_ENABLE_RX	0x2

#define XR21V141X_UART_MODE_RI		BIT(0)
#define XR21V141X_UART_MODE_CD		BIT(1)
#define XR21V141X_UART_MODE_DSR		BIT(2)
#define XR21V141X_UART_MODE_DTR		BIT(3)
#define XR21V141X_UART_MODE_CTS		BIT(4)
#define XR21V141X_UART_MODE_RTS		BIT(5)

#define XR21V141X_UART_BREAK_ON		0xff
#define XR21V141X_UART_BREAK_OFF	0

#define XR21V141X_UART_DATA_MASK	GENMASK(3, 0)
#define XR21V141X_UART_DATA_7		0x7
#define XR21V141X_UART_DATA_8		0x8

#define XR21V141X_UART_PARITY_MASK	GENMASK(6, 4)
#define XR21V141X_UART_PARITY_SHIFT	4
#define XR21V141X_UART_PARITY_NONE	(0x0 << XR21V141X_UART_PARITY_SHIFT)
#define XR21V141X_UART_PARITY_ODD	(0x1 << XR21V141X_UART_PARITY_SHIFT)
#define XR21V141X_UART_PARITY_EVEN	(0x2 << XR21V141X_UART_PARITY_SHIFT)
#define XR21V141X_UART_PARITY_MARK	(0x3 << XR21V141X_UART_PARITY_SHIFT)
#define XR21V141X_UART_PARITY_SPACE	(0x4 << XR21V141X_UART_PARITY_SHIFT)

#define XR21V141X_UART_STOP_MASK	BIT(7)
#define XR21V141X_UART_STOP_SHIFT	7
#define XR21V141X_UART_STOP_1		(0x0 << XR21V141X_UART_STOP_SHIFT)
#define XR21V141X_UART_STOP_2		(0x1 << XR21V141X_UART_STOP_SHIFT)

#define XR21V141X_UART_FLOW_MODE_NONE	0x0
#define XR21V141X_UART_FLOW_MODE_HW	0x1
#define XR21V141X_UART_FLOW_MODE_SW	0x2

#define XR21V141X_UART_MODE_GPIO_MASK	GENMASK(2, 0)
#define XR21V141X_UART_MODE_RTS_CTS	0x1
#define XR21V141X_UART_MODE_DTR_DSR	0x2
#define XR21V141X_UART_MODE_RS485	0x3
#define XR21V141X_UART_MODE_RS485_ADDR	0x4

#define XR21V141X_REG_ENABLE		0x03
#define XR21V141X_REG_FORMAT		0x0b
#define XR21V141X_REG_FLOW_CTRL		0x0c
#define XR21V141X_REG_XON_CHAR		0x10
#define XR21V141X_REG_XOFF_CHAR		0x11
#define XR21V141X_REG_LOOPBACK		0x12
#define XR21V141X_REG_TX_BREAK		0x14
#define XR21V141X_REG_RS845_DELAY	0x15
#define XR21V141X_REG_GPIO_MODE		0x1a
#define XR21V141X_REG_GPIO_DIR		0x1b
#define XR21V141X_REG_GPIO_INT_MASK	0x1c
#define XR21V141X_REG_GPIO_SET		0x1d
#define XR21V141X_REG_GPIO_CLR		0x1e
#define XR21V141X_REG_GPIO_STATUS	0x1f

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
	return xr_set_reg(port, XR21V141X_UART_REG_BLOCK, reg, val);
}

static int xr_get_reg_uart(struct usb_serial_port *port, u8 reg, u8 *val)
{
	return xr_get_reg(port, XR21V141X_UART_REG_BLOCK, reg, val);
}

static int xr_set_reg_um(struct usb_serial_port *port, u8 reg, u8 val)
{
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
	int ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_FIFO_ENABLE_REG,
			    XR21V141X_UM_ENABLE_TX_FIFO);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, XR21V141X_REG_ENABLE,
			      XR21V141X_UART_ENABLE_TX | XR21V141X_UART_ENABLE_RX);
	if (ret)
		return ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_FIFO_ENABLE_REG,
			    XR21V141X_UM_ENABLE_TX_FIFO | XR21V141X_UM_ENABLE_RX_FIFO);

	if (ret)
		xr_set_reg_uart(port, XR21V141X_REG_ENABLE, 0);

	return ret;
}

static int xr_uart_disable(struct usb_serial_port *port)
{
	int ret;

	ret = xr_set_reg_uart(port, XR21V141X_REG_ENABLE, 0);
	if (ret)
		return ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_FIFO_ENABLE_REG, 0);

	return ret;
}

static int xr_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	u8 status;
	int ret;

	ret = xr_get_reg_uart(port, XR21V141X_REG_GPIO_STATUS, &status);
	if (ret)
		return ret;

	/*
	 * Modem control pins are active low, so reading '0' means it is active
	 * and '1' means not active.
	 */
	ret = ((status & XR21V141X_UART_MODE_DTR) ? 0 : TIOCM_DTR) |
	      ((status & XR21V141X_UART_MODE_RTS) ? 0 : TIOCM_RTS) |
	      ((status & XR21V141X_UART_MODE_CTS) ? 0 : TIOCM_CTS) |
	      ((status & XR21V141X_UART_MODE_DSR) ? 0 : TIOCM_DSR) |
	      ((status & XR21V141X_UART_MODE_RI) ? 0 : TIOCM_RI) |
	      ((status & XR21V141X_UART_MODE_CD) ? 0 : TIOCM_CD);

	return ret;
}

static int xr_tiocmset_port(struct usb_serial_port *port,
			    unsigned int set, unsigned int clear)
{
	u8 gpio_set = 0;
	u8 gpio_clr = 0;
	int ret = 0;

	/* Modem control pins are active low, so set & clr are swapped */
	if (set & TIOCM_RTS)
		gpio_clr |= XR21V141X_UART_MODE_RTS;
	if (set & TIOCM_DTR)
		gpio_clr |= XR21V141X_UART_MODE_DTR;
	if (clear & TIOCM_RTS)
		gpio_set |= XR21V141X_UART_MODE_RTS;
	if (clear & TIOCM_DTR)
		gpio_set |= XR21V141X_UART_MODE_DTR;

	/* Writing '0' to gpio_{set/clr} bits has no effect, so no need to do */
	if (gpio_clr)
		ret = xr_set_reg_uart(port, XR21V141X_REG_GPIO_CLR, gpio_clr);

	if (gpio_set)
		ret = xr_set_reg_uart(port, XR21V141X_REG_GPIO_SET, gpio_set);

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
	u8 state;

	if (break_state == 0)
		state = XR21V141X_UART_BREAK_OFF;
	else
		state = XR21V141X_UART_BREAK_ON;

	dev_dbg(&port->dev, "Turning break %s\n",
		state == XR21V141X_UART_BREAK_OFF ? "off" : "on");
	xr_set_reg_uart(port, XR21V141X_REG_TX_BREAK, state);
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
	u8 flow, gpio_mode;
	int ret;

	ret = xr_get_reg_uart(port, XR21V141X_REG_GPIO_MODE, &gpio_mode);
	if (ret)
		return;

	/* Set GPIO mode for controlling the pins manually by default. */
	gpio_mode &= ~XR21V141X_UART_MODE_GPIO_MASK;

	if (C_CRTSCTS(tty) && C_BAUD(tty) != B0) {
		dev_dbg(&port->dev, "Enabling hardware flow ctrl\n");
		gpio_mode |= XR21V141X_UART_MODE_RTS_CTS;
		flow = XR21V141X_UART_FLOW_MODE_HW;
	} else if (I_IXON(tty)) {
		u8 start_char = START_CHAR(tty);
		u8 stop_char = STOP_CHAR(tty);

		dev_dbg(&port->dev, "Enabling sw flow ctrl\n");
		flow = XR21V141X_UART_FLOW_MODE_SW;

		xr_set_reg_uart(port, XR21V141X_REG_XON_CHAR, start_char);
		xr_set_reg_uart(port, XR21V141X_REG_XOFF_CHAR, stop_char);
	} else {
		dev_dbg(&port->dev, "Disabling flow ctrl\n");
		flow = XR21V141X_UART_FLOW_MODE_NONE;
	}

	/*
	 * As per the datasheet, UART needs to be disabled while writing to
	 * FLOW_CONTROL register.
	 */
	xr_uart_disable(port);
	xr_set_reg_uart(port, XR21V141X_REG_FLOW_CTRL, flow);
	xr_uart_enable(port);

	xr_set_reg_uart(port, XR21V141X_REG_GPIO_MODE, gpio_mode);

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
			bits |= XR21V141X_UART_DATA_7;
		else
			bits |= XR21V141X_UART_DATA_8;
		break;
	case CS7:
		bits |= XR21V141X_UART_DATA_7;
		break;
	case CS8:
	default:
		bits |= XR21V141X_UART_DATA_8;
		break;
	}

	if (C_PARENB(tty)) {
		if (C_CMSPAR(tty)) {
			if (C_PARODD(tty))
				bits |= XR21V141X_UART_PARITY_MARK;
			else
				bits |= XR21V141X_UART_PARITY_SPACE;
		} else {
			if (C_PARODD(tty))
				bits |= XR21V141X_UART_PARITY_ODD;
			else
				bits |= XR21V141X_UART_PARITY_EVEN;
		}
	}

	if (C_CSTOPB(tty))
		bits |= XR21V141X_UART_STOP_2;
	else
		bits |= XR21V141X_UART_STOP_1;

	ret = xr_set_reg_uart(port, XR21V141X_REG_FORMAT, bits);
	if (ret)
		return;

	xr_set_flow_mode(tty, port, old_termios);
}

static int xr_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	u8 gpio_dir;
	int ret;

	ret = xr_uart_enable(port);
	if (ret) {
		dev_err(&port->dev, "Failed to enable UART\n");
		return ret;
	}

	/*
	 * Configure DTR and RTS as outputs and RI, CD, DSR and CTS as
	 * inputs.
	 */
	gpio_dir = XR21V141X_UART_MODE_DTR | XR21V141X_UART_MODE_RTS;
	xr_set_reg_uart(port, XR21V141X_REG_GPIO_DIR, gpio_dir);

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
	/* Don't bind to control interface */
	if (serial->interface->cur_altsetting->desc.bInterfaceNumber == 0)
		return -ENODEV;

	return 0;
}

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x04e2, 0x1410) }, /* XR21V141X */
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
