// SPDX-License-Identifier: GPL-2.0+
/*
 * MaxLinear/Exar USB to Serial driver
 *
 * Copyright (c) 2020 Manivannan Sadhasivam <mani@kernel.org>
 * Copyright (c) 2021 Johan Hovold <johan@kernel.org>
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
#define XR_GPIO_CLK			BIT(6)
#define XR_GPIO_XEN			BIT(7)
#define XR_GPIO_TXT			BIT(8)
#define XR_GPIO_RXT			BIT(9)

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

#define XR_GPIO_MODE_SEL_MASK		GENMASK(2, 0)
#define XR_GPIO_MODE_SEL_RTS_CTS	0x1
#define XR_GPIO_MODE_SEL_DTR_DSR	0x2
#define XR_GPIO_MODE_SEL_RS485		0x3
#define XR_GPIO_MODE_SEL_RS485_ADDR	0x4
#define XR_GPIO_MODE_TX_TOGGLE		0x100
#define XR_GPIO_MODE_RX_TOGGLE		0x200

#define XR_FIFO_RESET			0x1

#define XR_CUSTOM_DRIVER_ACTIVE		0x1

static int xr21v141x_uart_enable(struct usb_serial_port *port);
static int xr21v141x_uart_disable(struct usb_serial_port *port);
static int xr21v141x_fifo_reset(struct usb_serial_port *port);
static void xr21v141x_set_line_settings(struct tty_struct *tty,
					struct usb_serial_port *port,
					const struct ktermios *old_termios);

struct xr_type {
	int reg_width;
	u8 reg_recipient;
	u8 set_reg;
	u8 get_reg;

	u16 uart_enable;
	u16 flow_control;
	u16 xon_char;
	u16 xoff_char;
	u16 tx_break;
	u16 gpio_mode;
	u16 gpio_direction;
	u16 gpio_set;
	u16 gpio_clear;
	u16 gpio_status;
	u16 tx_fifo_reset;
	u16 rx_fifo_reset;
	u16 custom_driver;

	bool have_5_6_bit_mode;
	bool have_xmit_toggle;

	int (*enable)(struct usb_serial_port *port);
	int (*disable)(struct usb_serial_port *port);
	int (*fifo_reset)(struct usb_serial_port *port);
	void (*set_line_settings)(struct tty_struct *tty,
				  struct usb_serial_port *port,
				  const struct ktermios *old_termios);
};

enum xr_type_id {
	XR21V141X,
	XR21B142X,
	XR21B1411,
	XR2280X,
	XR_TYPE_COUNT,
};

static const struct xr_type xr_types[] = {
	[XR21V141X] = {
		.reg_width	= 8,
		.reg_recipient	= USB_RECIP_DEVICE,
		.set_reg	= 0x00,
		.get_reg	= 0x01,

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

		.enable			= xr21v141x_uart_enable,
		.disable		= xr21v141x_uart_disable,
		.fifo_reset		= xr21v141x_fifo_reset,
		.set_line_settings	= xr21v141x_set_line_settings,
	},
	[XR21B142X] = {
		.reg_width	= 16,
		.reg_recipient	= USB_RECIP_INTERFACE,
		.set_reg	= 0x00,
		.get_reg	= 0x00,

		.uart_enable	= 0x00,
		.flow_control	= 0x06,
		.xon_char	= 0x07,
		.xoff_char	= 0x08,
		.tx_break	= 0x0a,
		.gpio_mode	= 0x0c,
		.gpio_direction	= 0x0d,
		.gpio_set	= 0x0e,
		.gpio_clear	= 0x0f,
		.gpio_status	= 0x10,
		.tx_fifo_reset	= 0x40,
		.rx_fifo_reset	= 0x43,
		.custom_driver	= 0x60,

		.have_5_6_bit_mode	= true,
		.have_xmit_toggle	= true,
	},
	[XR21B1411] = {
		.reg_width	= 12,
		.reg_recipient	= USB_RECIP_DEVICE,
		.set_reg	= 0x00,
		.get_reg	= 0x01,

		.uart_enable	= 0xc00,
		.flow_control	= 0xc06,
		.xon_char	= 0xc07,
		.xoff_char	= 0xc08,
		.tx_break	= 0xc0a,
		.gpio_mode	= 0xc0c,
		.gpio_direction	= 0xc0d,
		.gpio_set	= 0xc0e,
		.gpio_clear	= 0xc0f,
		.gpio_status	= 0xc10,
		.tx_fifo_reset	= 0xc80,
		.rx_fifo_reset	= 0xcc0,
		.custom_driver	= 0x20d,
	},
	[XR2280X] = {
		.reg_width	= 16,
		.reg_recipient	= USB_RECIP_DEVICE,
		.set_reg	= 0x05,
		.get_reg	= 0x05,

		.uart_enable	= 0x40,
		.flow_control	= 0x46,
		.xon_char	= 0x47,
		.xoff_char	= 0x48,
		.tx_break	= 0x4a,
		.gpio_mode	= 0x4c,
		.gpio_direction	= 0x4d,
		.gpio_set	= 0x4e,
		.gpio_clear	= 0x4f,
		.gpio_status	= 0x50,
		.tx_fifo_reset	= 0x60,
		.rx_fifo_reset	= 0x63,
		.custom_driver	= 0x81,
	},
};

struct xr_data {
	const struct xr_type *type;
	u8 channel;			/* zero-based index or interface number */
};

static int xr_set_reg(struct usb_serial_port *port, u8 channel, u16 reg, u16 val)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	const struct xr_type *type = data->type;
	struct usb_serial *serial = port->serial;
	int ret;

	ret = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			type->set_reg,
			USB_DIR_OUT | USB_TYPE_VENDOR | type->reg_recipient,
			val, (channel << 8) | reg, NULL, 0,
			USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&port->dev, "Failed to set reg 0x%02x: %d\n", reg, ret);
		return ret;
	}

	return 0;
}

static int xr_get_reg(struct usb_serial_port *port, u8 channel, u16 reg, u16 *val)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	const struct xr_type *type = data->type;
	struct usb_serial *serial = port->serial;
	u8 *dmabuf;
	int ret, len;

	if (type->reg_width == 8)
		len = 1;
	else
		len = 2;

	dmabuf = kmalloc(len, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	ret = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			type->get_reg,
			USB_DIR_IN | USB_TYPE_VENDOR | type->reg_recipient,
			0, (channel << 8) | reg, dmabuf, len,
			USB_CTRL_GET_TIMEOUT);
	if (ret == len) {
		if (len == 2)
			*val = le16_to_cpup((__le16 *)dmabuf);
		else
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

static int xr_set_reg_uart(struct usb_serial_port *port, u16 reg, u16 val)
{
	struct xr_data *data = usb_get_serial_port_data(port);

	return xr_set_reg(port, data->channel, reg, val);
}

static int xr_get_reg_uart(struct usb_serial_port *port, u16 reg, u16 *val)
{
	struct xr_data *data = usb_get_serial_port_data(port);

	return xr_get_reg(port, data->channel, reg, val);
}

static int xr_set_reg_um(struct usb_serial_port *port, u8 reg_base, u8 val)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	u8 reg;

	reg = reg_base + data->channel;

	return xr_set_reg(port, XR21V141X_UM_REG_BLOCK, reg, val);
}

static int __xr_uart_enable(struct usb_serial_port *port)
{
	struct xr_data *data = usb_get_serial_port_data(port);

	return xr_set_reg_uart(port, data->type->uart_enable,
			XR_UART_ENABLE_TX | XR_UART_ENABLE_RX);
}

static int __xr_uart_disable(struct usb_serial_port *port)
{
	struct xr_data *data = usb_get_serial_port_data(port);

	return xr_set_reg_uart(port, data->type->uart_enable, 0);
}

/*
 * According to datasheet, below is the recommended sequence for enabling UART
 * module in XR21V141X:
 *
 * Enable Tx FIFO
 * Enable Tx and Rx
 * Enable Rx FIFO
 */
static int xr21v141x_uart_enable(struct usb_serial_port *port)
{
	int ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_FIFO_ENABLE_REG,
			    XR21V141X_UM_ENABLE_TX_FIFO);
	if (ret)
		return ret;

	ret = __xr_uart_enable(port);
	if (ret)
		return ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_FIFO_ENABLE_REG,
			    XR21V141X_UM_ENABLE_TX_FIFO | XR21V141X_UM_ENABLE_RX_FIFO);
	if (ret)
		__xr_uart_disable(port);

	return ret;
}

static int xr21v141x_uart_disable(struct usb_serial_port *port)
{
	int ret;

	ret = __xr_uart_disable(port);
	if (ret)
		return ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_FIFO_ENABLE_REG, 0);

	return ret;
}

static int xr_uart_enable(struct usb_serial_port *port)
{
	struct xr_data *data = usb_get_serial_port_data(port);

	if (data->type->enable)
		return data->type->enable(port);

	return __xr_uart_enable(port);
}

static int xr_uart_disable(struct usb_serial_port *port)
{
	struct xr_data *data = usb_get_serial_port_data(port);

	if (data->type->disable)
		return data->type->disable(port);

	return __xr_uart_disable(port);
}

static int xr21v141x_fifo_reset(struct usb_serial_port *port)
{
	int ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_TX_FIFO_RESET, XR_FIFO_RESET);
	if (ret)
		return ret;

	ret = xr_set_reg_um(port, XR21V141X_UM_RX_FIFO_RESET, XR_FIFO_RESET);
	if (ret)
		return ret;

	return 0;
}

static int xr_fifo_reset(struct usb_serial_port *port)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	int ret;

	if (data->type->fifo_reset)
		return data->type->fifo_reset(port);

	ret = xr_set_reg_uart(port, data->type->tx_fifo_reset, XR_FIFO_RESET);
	if (ret)
		return ret;

	ret = xr_set_reg_uart(port, data->type->rx_fifo_reset, XR_FIFO_RESET);
	if (ret)
		return ret;

	return 0;
}

static int xr_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct xr_data *data = usb_get_serial_port_data(port);
	u16 status;
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
	u16 gpio_set = 0;
	u16 gpio_clr = 0;
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
	u16 state;

	if (break_state == 0)
		state = 0;
	else
		state = GENMASK(type->reg_width - 1, 0);

	dev_dbg(&port->dev, "Turning break %s\n", state == 0 ? "off" : "on");

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

static int xr21v141x_set_baudrate(struct tty_struct *tty, struct usb_serial_port *port)
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
		             const struct ktermios *old_termios)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	const struct xr_type *type = data->type;
	u16 flow, gpio_mode;
	int ret;

	ret = xr_get_reg_uart(port, type->gpio_mode, &gpio_mode);
	if (ret)
		return;

	/*
	 * According to the datasheets, the UART needs to be disabled while
	 * writing to the FLOW_CONTROL register (XR21V141X), or any register
	 * but GPIO_SET, GPIO_CLEAR, TX_BREAK and ERROR_STATUS (XR21B142X).
	 */
	xr_uart_disable(port);

	/* Set GPIO mode for controlling the pins manually by default. */
	gpio_mode &= ~XR_GPIO_MODE_SEL_MASK;

	if (C_CRTSCTS(tty) && C_BAUD(tty) != B0) {
		dev_dbg(&port->dev, "Enabling hardware flow ctrl\n");
		gpio_mode |= XR_GPIO_MODE_SEL_RTS_CTS;
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

	xr_set_reg_uart(port, type->flow_control, flow);
	xr_set_reg_uart(port, type->gpio_mode, gpio_mode);

	xr_uart_enable(port);

	if (C_BAUD(tty) == B0)
		xr_dtr_rts(port, 0);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		xr_dtr_rts(port, 1);
}

static void xr21v141x_set_line_settings(struct tty_struct *tty,
				        struct usb_serial_port *port,
				        const struct ktermios *old_termios)
{
	struct ktermios *termios = &tty->termios;
	u8 bits = 0;
	int ret;

	if (!old_termios || (tty->termios.c_ospeed != old_termios->c_ospeed))
		xr21v141x_set_baudrate(tty, port);

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
}

static void xr_cdc_set_line_coding(struct tty_struct *tty,
				   struct usb_serial_port *port,
				   const struct ktermios *old_termios)
{
	struct xr_data *data = usb_get_serial_port_data(port);
	struct usb_host_interface *alt = port->serial->interface->cur_altsetting;
	struct usb_device *udev = port->serial->dev;
	struct usb_cdc_line_coding *lc;
	int ret;

	lc = kzalloc(sizeof(*lc), GFP_KERNEL);
	if (!lc)
		return;

	if (tty->termios.c_ospeed)
		lc->dwDTERate = cpu_to_le32(tty->termios.c_ospeed);
	else
		lc->dwDTERate = cpu_to_le32(9600);

	if (C_CSTOPB(tty))
		lc->bCharFormat = USB_CDC_2_STOP_BITS;
	else
		lc->bCharFormat = USB_CDC_1_STOP_BITS;

	if (C_PARENB(tty)) {
		if (C_CMSPAR(tty)) {
			if (C_PARODD(tty))
				lc->bParityType = USB_CDC_MARK_PARITY;
			else
				lc->bParityType = USB_CDC_SPACE_PARITY;
		} else {
			if (C_PARODD(tty))
				lc->bParityType = USB_CDC_ODD_PARITY;
			else
				lc->bParityType = USB_CDC_EVEN_PARITY;
		}
	} else {
		lc->bParityType = USB_CDC_NO_PARITY;
	}

	if (!data->type->have_5_6_bit_mode &&
			(C_CSIZE(tty) == CS5 || C_CSIZE(tty) == CS6)) {
		tty->termios.c_cflag &= ~CSIZE;
		if (old_termios)
			tty->termios.c_cflag |= old_termios->c_cflag & CSIZE;
		else
			tty->termios.c_cflag |= CS8;
	}

	switch (C_CSIZE(tty)) {
	case CS5:
		lc->bDataBits = 5;
		break;
	case CS6:
		lc->bDataBits = 6;
		break;
	case CS7:
		lc->bDataBits = 7;
		break;
	case CS8:
	default:
		lc->bDataBits = 8;
		break;
	}

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			USB_CDC_REQ_SET_LINE_CODING,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, alt->desc.bInterfaceNumber,
			lc, sizeof(*lc), USB_CTRL_SET_TIMEOUT);
	if (ret < 0)
		dev_err(&port->dev, "Failed to set line coding: %d\n", ret);

	kfree(lc);
}

static void xr_set_termios(struct tty_struct *tty,
			   struct usb_serial_port *port,
			   const struct ktermios *old_termios)
{
	struct xr_data *data = usb_get_serial_port_data(port);

	/*
	 * XR21V141X does not have a CUSTOM_DRIVER flag and always enters CDC
	 * mode upon receiving CDC requests.
	 */
	if (data->type->set_line_settings)
		data->type->set_line_settings(tty, port, old_termios);
	else
		xr_cdc_set_line_coding(tty, port, old_termios);

	xr_set_flow_mode(tty, port, old_termios);
}

static int xr_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int ret;

	ret = xr_fifo_reset(port);
	if (ret)
		return ret;

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
	u16 mask, mode;
	int ret;

	/*
	 * Configure all pins as GPIO except for Receive and Transmit Toggle.
	 */
	mode = 0;
	if (type->have_xmit_toggle)
		mode |= XR_GPIO_MODE_RX_TOGGLE | XR_GPIO_MODE_TX_TOGGLE;

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
	if (type_id == XR21V141X)
		data->channel = desc->bInterfaceNumber / 2;
	else
		data->channel = desc->bInterfaceNumber;

	usb_set_serial_port_data(port, data);

	if (type->custom_driver) {
		ret = xr_set_reg_uart(port, type->custom_driver,
				XR_CUSTOM_DRIVER_ACTIVE);
		if (ret)
			goto err_free;
	}

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
	{ XR_DEVICE(0x04e2, 0x1400, XR2280X) },
	{ XR_DEVICE(0x04e2, 0x1401, XR2280X) },
	{ XR_DEVICE(0x04e2, 0x1402, XR2280X) },
	{ XR_DEVICE(0x04e2, 0x1403, XR2280X) },
	{ XR_DEVICE(0x04e2, 0x1410, XR21V141X) },
	{ XR_DEVICE(0x04e2, 0x1411, XR21B1411) },
	{ XR_DEVICE(0x04e2, 0x1412, XR21V141X) },
	{ XR_DEVICE(0x04e2, 0x1414, XR21V141X) },
	{ XR_DEVICE(0x04e2, 0x1420, XR21B142X) },
	{ XR_DEVICE(0x04e2, 0x1422, XR21B142X) },
	{ XR_DEVICE(0x04e2, 0x1424, XR21B142X) },
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
