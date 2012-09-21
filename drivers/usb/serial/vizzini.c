/*
 * vizzini.c
 *
 * Copyright (c) 2011 Exar Corporation, Inc.
 *
 * ChangeLog:
 *            v0.76- Support for 3.0.0 (Ubuntu 11.10) (Removed all Kernel source
 *                   compiler conditions and now the base is Kernel 3.0. Ravi Reddy)
 *            v0.75- Support for 2.6.38.8 (Ubuntu 11.04) - Added
 *                   .usb_driver = &vizzini_driver.
 *            v0.74- Support for 2.6.35.22 (Ubuntu 10.10) - Added
 *                   #include <linux/slab.h> to fix kmalloc/kfree error.
 *            v0.73- Fixed VZIOC_SET_REG (by Ravi Reddy).
 *            v0.72- Support for 2.6.32.21 (by Ravi Reddy, for Ubuntu 10.04).
 *            v0.71- Support for 2.6.31.
 *            v0.5 - Tentative support for compiling with the CentOS 5.1
 *                   kernel (2.6.18-53).
 *            v0.4 - First version.  Lots of stuff lifted from
 *                   cdc-acm.c (credits due to Armin Fuerst, Pavel Machek,
 *                   Johannes Erdfelt, Vojtech Pavlik, David Kubicek) and
 *                   and sierra.c (credit due to Kevin Lloyd).
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define DRIVER_VERSION "v.0.76"
#define DRIVER_AUTHOR "Rob Duncan <rob.duncan@exar.com>"
#define DRIVER_DESC "USB Driver for Vizzini USB serial port"

#undef VIZZINI_IWA


#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include <linux/usb/cdc.h>
#ifndef CDC_DATA_INTERFACE_TYPE
#define CDC_DATA_INTERFACE_TYPE 0x0a
#endif
#ifndef USB_RT_ACM
#define USB_RT_ACM      (USB_TYPE_CLASS | USB_RECIP_INTERFACE)
#define ACM_CTRL_DTR            0x01
#define ACM_CTRL_RTS            0x02
#define ACM_CTRL_DCD            0x01
#define ACM_CTRL_DSR            0x02
#define ACM_CTRL_BRK            0x04
#define ACM_CTRL_RI             0x08
#define ACM_CTRL_FRAMING        0x10
#define ACM_CTRL_PARITY         0x20
#define ACM_CTRL_OVERRUN        0x40
#endif

#define XR_SET_REG              0
#define XR_GETN_REG             1

#define UART_0_REG_BLOCK        0
#define UART_1_REG_BLOCK        1
#define UART_2_REG_BLOCK        2
#define UART_3_REG_BLOCK        3
#define URM_REG_BLOCK           4
#define PRM_REG_BLOCK           5
#define EPMERR_REG_BLOCK        6
#define RAMCTL_REG_BLOCK        0x64
#define TWI_ROM_REG_BLOCK       0x65
#define EPLOCALS_REG_BLOCK      0x66

#define MEM_SHADOW_REG_SIZE_S   5
#define MEM_SHADOW_REG_SIZE     (1 << MEM_SHADOW_REG_SIZE_S)

#define MEM_EP_LOCALS_SIZE_S    3
#define MEM_EP_LOCALS_SIZE      (1 << MEM_EP_LOCALS_SIZE_S)

#define EP_WIDE_MODE            0x03


#define UART_GPIO_MODE                                     0x01a

#define UART_GPIO_MODE_SEL_M                               0x7
#define UART_GPIO_MODE_SEL_S                               0
#define UART_GPIO_MODE_SEL                                 0x007

#define UART_GPIO_MODE_SEL_GPIO                            (0x0 << UART_GPIO_MODE_SEL_S)
#define UART_GPIO_MODE_SEL_RTS_CTS                         (0x1 << UART_GPIO_MODE_SEL_S)
#define UART_GPIO_MODE_SEL_DTR_DSR                         (0x2 << UART_GPIO_MODE_SEL_S)
#define UART_GPIO_MODE_SEL_XCVR_EN_ACT                     (0x3 << UART_GPIO_MODE_SEL_S)
#define UART_GPIO_MODE_SEL_XCVR_EN_FLOW                    (0x4 << UART_GPIO_MODE_SEL_S)

#define UART_GPIO_MODE_XCVR_EN_POL_M                       0x1
#define UART_GPIO_MODE_XCVR_EN_POL_S                       3
#define UART_GPIO_MODE_XCVR_EN_POL                         0x008

#define UART_ENABLE                                        0x003
#define UART_ENABLE_TX_M                                   0x1
#define UART_ENABLE_TX_S                                   0
#define UART_ENABLE_TX                                     0x001
#define UART_ENABLE_RX_M                                   0x1
#define UART_ENABLE_RX_S                                   1
#define UART_ENABLE_RX                                     0x002

#define UART_CLOCK_DIVISOR_0                               0x004
#define UART_CLOCK_DIVISOR_1                               0x005
#define UART_CLOCK_DIVISOR_2                               0x006

#define UART_CLOCK_DIVISOR_2_MSB_M                         0x7
#define UART_CLOCK_DIVISOR_2_MSB_S                         0
#define UART_CLOCK_DIVISOR_2_MSB                           0x007
#define UART_CLOCK_DIVISOR_2_DIAGMODE_M                    0x1
#define UART_CLOCK_DIVISOR_2_DIAGMODE_S                    3
#define UART_CLOCK_DIVISOR_2_DIAGMODE                      0x008

#define UART_TX_CLOCK_MASK_0                               0x007
#define UART_TX_CLOCK_MASK_1                               0x008

#define UART_RX_CLOCK_MASK_0                               0x009
#define UART_RX_CLOCK_MASK_1                               0x00a

#define UART_FORMAT                                        0x00b

#define UART_FORMAT_SIZE_M                                 0xf
#define UART_FORMAT_SIZE_S                                 0
#define UART_FORMAT_SIZE                                   0x00f

#define UART_FORMAT_SIZE_7                                 (0x7 << UART_FORMAT_SIZE_S)
#define UART_FORMAT_SIZE_8                                 (0x8 << UART_FORMAT_SIZE_S)
#define UART_FORMAT_SIZE_9                                 (0x9 << UART_FORMAT_SIZE_S)

#define UART_FORMAT_PARITY_M                               0x7
#define UART_FORMAT_PARITY_S                               4
#define UART_FORMAT_PARITY                                 0x070

#define UART_FORMAT_PARITY_NONE                            (0x0 << UART_FORMAT_PARITY_S)
#define UART_FORMAT_PARITY_ODD                             (0x1 << UART_FORMAT_PARITY_S)
#define UART_FORMAT_PARITY_EVEN                            (0x2 << UART_FORMAT_PARITY_S)
#define UART_FORMAT_PARITY_1                               (0x3 << UART_FORMAT_PARITY_S)
#define UART_FORMAT_PARITY_0                               (0x4 << UART_FORMAT_PARITY_S)

#define UART_FORMAT_STOP_M                                 0x1
#define UART_FORMAT_STOP_S                                 7
#define UART_FORMAT_STOP                                   0x080

#define UART_FORMAT_STOP_1                                 (0x0 << UART_FORMAT_STOP_S)
#define UART_FORMAT_STOP_2                                 (0x1 << UART_FORMAT_STOP_S)

#define UART_FORMAT_MODE_7N1                               0
#define UART_FORMAT_MODE_RES1                              1
#define UART_FORMAT_MODE_RES2                              2
#define UART_FORMAT_MODE_RES3                              3
#define UART_FORMAT_MODE_7N2                               4
#define UART_FORMAT_MODE_7P1                               5
#define UART_FORMAT_MODE_8N1                               6
#define UART_FORMAT_MODE_RES7                              7
#define UART_FORMAT_MODE_7P2                               8
#define UART_FORMAT_MODE_8N2                               9
#define UART_FORMAT_MODE_8P1                               10
#define UART_FORMAT_MODE_9N1                               11
#define UART_FORMAT_MODE_8P2                               12
#define UART_FORMAT_MODE_RESD                              13
#define UART_FORMAT_MODE_RESE                              14
#define UART_FORMAT_MODE_9N2                               15

#define UART_FLOW                                          0x00c

#define UART_FLOW_MODE_M                                   0x7
#define UART_FLOW_MODE_S                                   0
#define UART_FLOW_MODE                                     0x007

#define UART_FLOW_MODE_NONE                                (0x0 << UART_FLOW_MODE_S)
#define UART_FLOW_MODE_HW                                  (0x1 << UART_FLOW_MODE_S)
#define UART_FLOW_MODE_SW                                  (0x2 << UART_FLOW_MODE_S)
#define UART_FLOW_MODE_ADDR_MATCH                          (0x3 << UART_FLOW_MODE_S)
#define UART_FLOW_MODE_ADDR_MATCH_TX                       (0x4 << UART_FLOW_MODE_S)

#define UART_FLOW_HALF_DUPLEX_M                            0x1
#define UART_FLOW_HALF_DUPLEX_S                            3
#define UART_FLOW_HALF_DUPLEX                              0x008

#define UART_LOOPBACK_CTL                                  0x012
#define UART_LOOPBACK_CTL_ENABLE_M                         0x1
#define UART_LOOPBACK_CTL_ENABLE_S                         2
#define UART_LOOPBACK_CTL_ENABLE                           0x004
#define UART_LOOPBACK_CTL_RX_SOURCE_M                      0x3
#define UART_LOOPBACK_CTL_RX_SOURCE_S                      0
#define UART_LOOPBACK_CTL_RX_SOURCE                        0x003
#define UART_LOOPBACK_CTL_RX_UART0                         (0x0 << UART_LOOPBACK_CTL_RX_SOURCE_S)
#define UART_LOOPBACK_CTL_RX_UART1                         (0x1 << UART_LOOPBACK_CTL_RX_SOURCE_S)
#define UART_LOOPBACK_CTL_RX_UART2                         (0x2 << UART_LOOPBACK_CTL_RX_SOURCE_S)
#define UART_LOOPBACK_CTL_RX_UART3                         (0x3 << UART_LOOPBACK_CTL_RX_SOURCE_S)

#define UART_CHANNEL_NUM                                   0x00d

#define UART_XON_CHAR                                      0x010
#define UART_XOFF_CHAR                                     0x011

#define UART_GPIO_SET                                      0x01d
#define UART_GPIO_CLR                                      0x01e
#define UART_GPIO_STATUS                                   0x01f

#define URM_ENABLE_BASE                                    0x010
#define URM_ENABLE_0                                       0x010
#define URM_ENABLE_0_TX_M                                  0x1
#define URM_ENABLE_0_TX_S                                  0
#define URM_ENABLE_0_TX                                    0x001
#define URM_ENABLE_0_RX_M                                  0x1
#define URM_ENABLE_0_RX_S                                  1
#define URM_ENABLE_0_RX                                    0x002

#define URM_RX_FIFO_RESET_0                                0x018
#define URM_RX_FIFO_RESET_1                                0x019
#define URM_RX_FIFO_RESET_2                                0x01a
#define URM_RX_FIFO_RESET_3                                0x01b
#define URM_TX_FIFO_RESET_0                                0x01c
#define URM_TX_FIFO_RESET_1                                0x01d
#define URM_TX_FIFO_RESET_2                                0x01e
#define URM_TX_FIFO_RESET_3                                0x01f


#define RAMCTL_REGS_TXFIFO_0_LEVEL                         0x000
#define RAMCTL_REGS_TXFIFO_1_LEVEL                         0x001
#define RAMCTL_REGS_TXFIFO_2_LEVEL                         0x002
#define RAMCTL_REGS_TXFIFO_3_LEVEL                         0x003
#define RAMCTL_REGS_RXFIFO_0_LEVEL                         0x004

#define RAMCTL_REGS_RXFIFO_0_LEVEL_LEVEL_M                 0x7ff
#define RAMCTL_REGS_RXFIFO_0_LEVEL_LEVEL_S                 0
#define RAMCTL_REGS_RXFIFO_0_LEVEL_LEVEL                   0x7ff
#define RAMCTL_REGS_RXFIFO_0_LEVEL_STALE_M                 0x1
#define RAMCTL_REGS_RXFIFO_0_LEVEL_STALE_S                 11
#define RAMCTL_REGS_RXFIFO_0_LEVEL_STALE                   0x800

#define RAMCTL_REGS_RXFIFO_1_LEVEL                         0x005
#define RAMCTL_REGS_RXFIFO_2_LEVEL                         0x006
#define RAMCTL_REGS_RXFIFO_3_LEVEL                         0x007

#define RAMCTL_BUFFER_PARITY                               0x1
#define RAMCTL_BUFFER_BREAK                                0x2
#define RAMCTL_BUFFER_FRAME                                0x4
#define RAMCTL_BUFFER_OVERRUN                              0x8

#define N_IN_URB	4
#define N_OUT_URB	4
#define IN_BUFLEN	4096

static struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x04e2, 0x1410) },
	{ USB_DEVICE(0x04e2, 0x1412) },
	{ USB_DEVICE(0x04e2, 0x1414) },
	{ }
};
MODULE_DEVICE_TABLE(usb, id_table);

struct vizzini_serial_private {
	struct usb_interface *data_interface;
};

struct vizzini_port_private {
	spinlock_t lock;
	int outstanding_urbs;

	struct urb *in_urbs[N_IN_URB];
	char *in_buffer[N_IN_URB];

	int ctrlin;
	int ctrlout;
	int clocal;

	int block;
	int preciseflags;	/* USB: wide mode, TTY: flags per character */
	int trans9;		/* USB: wide mode, serial 9N1 */
	unsigned int baud_base;	/* setserial: used to hack in non-standard baud rates */
	int have_extra_byte;
	int extra_byte;

	int bcd_device;

#ifdef VIZZINI_IWA
	int iwa;
#endif
};


static int vizzini_rev_a(struct usb_serial_port *port)
{
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	return portdata->bcd_device == 0;
}

static int acm_ctrl_msg(struct usb_serial_port *port, int request,
			int value, void *buf, int len)
{
	struct usb_serial *serial = port->serial;
	int retval = usb_control_msg(serial->dev,
				     usb_sndctrlpipe(serial->dev, 0),
				     request,
				     USB_RT_ACM,
				     value,
				     serial->interface->cur_altsetting->desc.bInterfaceNumber,
				     buf,
				     len,
				     5000);
	dev_dbg(&port->dev, "acm_control_msg: rq: 0x%02x val: %#x len: %#x result: %d\n", request, value, len, retval);
	return retval < 0 ? retval : 0;
}

#define acm_set_control(port, control)					\
	acm_ctrl_msg(port, USB_CDC_REQ_SET_CONTROL_LINE_STATE, control, NULL, 0)
#define acm_set_line(port, line)					\
	acm_ctrl_msg(port, USB_CDC_REQ_SET_LINE_CODING, 0, line, sizeof *(line))
#define acm_send_break(port, ms)					\
	acm_ctrl_msg(port, USB_CDC_REQ_SEND_BREAK, ms, NULL, 0)

static int vizzini_set_reg(struct usb_serial_port *port, int block, int regnum, int value)
{
	struct usb_serial *serial = port->serial;
	int result;

	result = usb_control_msg(serial->dev,                     /* usb device */
				 usb_sndctrlpipe(serial->dev, 0), /* endpoint pipe */
				 XR_SET_REG,                      /* request */
				 USB_DIR_OUT | USB_TYPE_VENDOR,   /* request_type */
				 value,                           /* request value */
				 regnum | (block << 8),           /* index */
				 NULL,                            /* data */
				 0,                               /* size */
				 5000);                           /* timeout */

	return result;
}

static void vizzini_disable(struct usb_serial_port *port)
{
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	int block = portdata->block;

	vizzini_set_reg(port, block, UART_ENABLE, 0);
	vizzini_set_reg(port, URM_REG_BLOCK, URM_ENABLE_BASE + block, 0);
}

static void vizzini_enable(struct usb_serial_port *port)
{
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	int block = portdata->block;

	vizzini_set_reg(port, URM_REG_BLOCK, URM_ENABLE_BASE + block, URM_ENABLE_0_TX);
	vizzini_set_reg(port, block, UART_ENABLE, UART_ENABLE_TX | UART_ENABLE_RX);
	vizzini_set_reg(port, URM_REG_BLOCK, URM_ENABLE_BASE + block, URM_ENABLE_0_TX | URM_ENABLE_0_RX);
}

struct vizzini_baud_rate {
	unsigned int tx;
	unsigned int rx0;
	unsigned int rx1;
};

static struct vizzini_baud_rate vizzini_baud_rates[] = {
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
	{ 0X94A, 0X4A4, 0XA52 },
	{ 0X52A, 0XAA4, 0X54A },
	{ 0XAAA, 0x954, 0X4AA },
	{ 0XAAA, 0x554, 0XAAA },
	{ 0x555, 0XAD4, 0X5AA },
	{ 0XB55, 0XAB4, 0X55A },
	{ 0X6B5, 0X5AC, 0XB56 },
	{ 0X5B5, 0XD6C, 0X6D6 },
	{ 0XB6D, 0XB6A, 0XDB6 },
	{ 0X76D, 0X6DA, 0XBB6 },
	{ 0XEDD, 0XDDA, 0X76E },
	{ 0XDDD, 0XBBA, 0XEEE },
	{ 0X7BB, 0XF7A, 0XDDE },
	{ 0XF7B, 0XEF6, 0X7DE },
	{ 0XDF7, 0XBF6, 0XF7E },
	{ 0X7F7, 0XFEE, 0XEFE },
	{ 0XFDF, 0XFBE, 0X7FE },
	{ 0XF7F, 0XEFE, 0XFFE },
	{ 0XFFF, 0XFFE, 0XFFD },
};

static int vizzini_set_baud_rate(struct usb_serial_port *port, unsigned int rate)
{
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	int block = portdata->block;
	unsigned int divisor = 48000000 / rate;
	unsigned int i = ((32 * 48000000) / rate) & 0x1f;
	unsigned int tx_mask = vizzini_baud_rates[i].tx;
	unsigned int rx_mask = (divisor & 1) ? vizzini_baud_rates[i].rx1 : vizzini_baud_rates[i].rx0;

	dev_dbg(&port->dev, "Setting baud rate to %d: i=%u div=%u tx=%03x rx=%03x\n", rate, i, divisor, tx_mask, rx_mask);

	vizzini_set_reg(port, block, UART_CLOCK_DIVISOR_0, (divisor >>  0) & 0xff);
	vizzini_set_reg(port, block, UART_CLOCK_DIVISOR_1, (divisor >>  8) & 0xff);
	vizzini_set_reg(port, block, UART_CLOCK_DIVISOR_2, (divisor >> 16) & 0xff);
	vizzini_set_reg(port, block, UART_TX_CLOCK_MASK_0, (tx_mask >>  0) & 0xff);
	vizzini_set_reg(port, block, UART_TX_CLOCK_MASK_1, (tx_mask >>  8) & 0xff);
	vizzini_set_reg(port, block, UART_RX_CLOCK_MASK_0, (rx_mask >>  0) & 0xff);
	vizzini_set_reg(port, block, UART_RX_CLOCK_MASK_1, (rx_mask >>  8) & 0xff);

	return -EINVAL;
}

static void vizzini_set_termios(struct tty_struct *tty_param,
				struct usb_serial_port *port,
				struct ktermios *old_termios)
{
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	unsigned int             cflag, block;
	speed_t                  rate;
	unsigned int             format_size, format_parity, format_stop, flow, gpio_mode;
	struct tty_struct       *tty = port->port.tty;

	cflag = tty->termios->c_cflag;

	portdata->clocal = ((cflag & CLOCAL) != 0);

	block = portdata->block;

	vizzini_disable(port);

	if ((cflag & CSIZE) == CS7) {
		format_size = UART_FORMAT_SIZE_7;
	} else if ((cflag & CSIZE) == CS5) {
		/* Enabling 5-bit mode is really 9-bit mode! */
		format_size = UART_FORMAT_SIZE_9;
	} else {
		format_size = UART_FORMAT_SIZE_8;
	}
	portdata->trans9 = (format_size == UART_FORMAT_SIZE_9);

	if (cflag & PARENB) {
		if (cflag & PARODD) {
			if (cflag & CMSPAR)
				format_parity = UART_FORMAT_PARITY_1;
			else
				format_parity = UART_FORMAT_PARITY_ODD;
		} else {
			if (cflag & CMSPAR)
				format_parity = UART_FORMAT_PARITY_0;
			else
				format_parity = UART_FORMAT_PARITY_EVEN;
		}
	} else {
		format_parity = UART_FORMAT_PARITY_NONE;
	}

	if (cflag & CSTOPB)
		format_stop = UART_FORMAT_STOP_2;
	else
		format_stop = UART_FORMAT_STOP_1;

#ifdef VIZZINI_IWA
	if (format_size == UART_FORMAT_SIZE_8) {
		portdata->iwa = format_parity;
		if (portdata->iwa != UART_FORMAT_PARITY_NONE) {
			format_size = UART_FORMAT_SIZE_9;
			format_parity = UART_FORMAT_PARITY_NONE;
		}
	} else {
		portdata->iwa = UART_FORMAT_PARITY_NONE;
	}
#endif
	vizzini_set_reg(port, block, UART_FORMAT, format_size | format_parity | format_stop);

	if (cflag & CRTSCTS) {
		flow      = UART_FLOW_MODE_HW;
		gpio_mode = UART_GPIO_MODE_SEL_RTS_CTS;
	} else if (I_IXOFF(tty) || I_IXON(tty)) {
		unsigned char   start_char = START_CHAR(tty);
		unsigned char   stop_char  = STOP_CHAR(tty);

		flow      = UART_FLOW_MODE_SW;
		gpio_mode = UART_GPIO_MODE_SEL_GPIO;

		vizzini_set_reg(port, block, UART_XON_CHAR, start_char);
		vizzini_set_reg(port, block, UART_XOFF_CHAR, stop_char);
	} else {
		flow      = UART_FLOW_MODE_NONE;
		gpio_mode = UART_GPIO_MODE_SEL_GPIO;
	}

	vizzini_set_reg(port, block, UART_FLOW, flow);
	vizzini_set_reg(port, block, UART_GPIO_MODE, gpio_mode);

	if (portdata->trans9) {
		/* Turn on wide mode if we're 9-bit transparent. */
		vizzini_set_reg(port, EPLOCALS_REG_BLOCK, (block * MEM_EP_LOCALS_SIZE) + EP_WIDE_MODE, 1);
#ifdef VIZZINI_IWA
	} else if (portdata->iwa != UART_FORMAT_PARITY_NONE) {
		vizzini_set_reg(port, EPLOCALS_REG_BLOCK, (block * MEM_EP_LOCALS_SIZE) + EP_WIDE_MODE, 1);
#endif
	} else if (!portdata->preciseflags) {
		/* Turn off wide mode unless we have precise flags. */
		vizzini_set_reg(port, EPLOCALS_REG_BLOCK, (block * MEM_EP_LOCALS_SIZE) + EP_WIDE_MODE, 0);
	}

	rate = tty_get_baud_rate(tty);
	if (rate)
		vizzini_set_baud_rate(port, rate);

	vizzini_enable(port);
}

static void vizzini_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;

	dev_dbg(&port->dev, "BREAK %d\n", break_state);
	if (break_state)
		acm_send_break(port, 0x10);
	else
		acm_send_break(port, 0x000);
}

static int vizzini_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);

	return (portdata->ctrlout & ACM_CTRL_DTR ? TIOCM_DTR : 0) |
		(portdata->ctrlout & ACM_CTRL_RTS ? TIOCM_RTS : 0) |
		(portdata->ctrlin  & ACM_CTRL_DSR ? TIOCM_DSR : 0) |
		(portdata->ctrlin  & ACM_CTRL_RI  ? TIOCM_RI  : 0) |
		(portdata->ctrlin  & ACM_CTRL_DCD ? TIOCM_CD  : 0) |
		TIOCM_CTS;
}

static int vizzini_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	unsigned int newctrl;

	newctrl = portdata->ctrlout;
	set = (set & TIOCM_DTR ? ACM_CTRL_DTR : 0) | (set & TIOCM_RTS ? ACM_CTRL_RTS : 0);
	clear = (clear & TIOCM_DTR ? ACM_CTRL_DTR : 0) | (clear & TIOCM_RTS ? ACM_CTRL_RTS : 0);

	newctrl = (newctrl & ~clear) | set;

	if (portdata->ctrlout == newctrl)
		return 0;
	return acm_set_control(port, portdata->ctrlout = newctrl);
}

static int vizzini_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	struct serial_struct ss;

	dev_dbg(&port->dev, "%s %08x\n", __func__, cmd);

	switch (cmd) {
	case TIOCGSERIAL:
		if (!arg)
			return -EFAULT;
		memset(&ss, 0, sizeof(ss));
		ss.baud_base = portdata->baud_base;
		if (copy_to_user((void __user *)arg, &ss, sizeof(ss)))
			return -EFAULT;
		break;

	case TIOCSSERIAL:
		if (!arg)
			return -EFAULT;
		if (copy_from_user(&ss, (void __user *)arg, sizeof(ss)))
			return -EFAULT;
		portdata->baud_base = ss.baud_base;
		dev_dbg(&port->dev, "baud_base=%d\n", portdata->baud_base);

		vizzini_disable(port);
		if (portdata->baud_base)
			vizzini_set_baud_rate(port, portdata->baud_base);
		vizzini_enable(port);
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

#ifdef VIZZINI_IWA
static const int vizzini_parity[] = {
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0
};
#endif

static void vizzini_out_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	int status = urb->status;
	unsigned long flags;

	dev_dbg(&port->dev, "%s - port %d\n", __func__, port->number);

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree(urb->transfer_buffer);

	if (status)
		dev_dbg(&port->dev, "%s - nonzero write bulk status received: %d\n", __func__, status);

	spin_lock_irqsave(&portdata->lock, flags);
	--portdata->outstanding_urbs;
	spin_unlock_irqrestore(&portdata->lock, flags);

	usb_serial_port_softint(port);
}

static int vizzini_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	unsigned long flags;

	dev_dbg(&port->dev, "%s - port %d\n", __func__, port->number);

	/* try to give a good number back based on if we have any free urbs at
	 * this point in time */
	spin_lock_irqsave(&portdata->lock, flags);
	if (portdata->outstanding_urbs > N_OUT_URB * 2 / 3) {
		spin_unlock_irqrestore(&portdata->lock, flags);
		dev_dbg(&port->dev, "%s - write limit hit\n", __func__);
		return 0;
	}
	spin_unlock_irqrestore(&portdata->lock, flags);

	return 2048;
}

static int vizzini_write(struct tty_struct *tty, struct usb_serial_port *port,
			 const unsigned char *buf, int count)
{
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	int bufsize = count;
	unsigned long flags;
	unsigned char *buffer;
	struct urb *urb;
	int status;

	portdata = usb_get_serial_port_data(port);

	dev_dbg(&port->dev, "%s: write (%d chars)\n", __func__, count);

	spin_lock_irqsave(&portdata->lock, flags);
	if (portdata->outstanding_urbs > N_OUT_URB) {
		spin_unlock_irqrestore(&portdata->lock, flags);
		dev_dbg(&port->dev, "%s - write limit hit\n", __func__);
		return 0;
	}
	portdata->outstanding_urbs++;
	spin_unlock_irqrestore(&portdata->lock, flags);

#ifdef VIZZINI_IWA
	if (portdata->iwa != UART_FORMAT_PARITY_NONE)
		bufsize = count * 2;
#endif
	buffer = kmalloc(bufsize, GFP_ATOMIC);

	if (!buffer) {
		dev_err(&port->dev, "out of memory\n");
		count = -ENOMEM;
		goto error_no_buffer;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		dev_err(&port->dev, "no more free urbs\n");
		count = -ENOMEM;
		goto error_no_urb;
	}

#ifdef VIZZINI_IWA
	if (portdata->iwa != UART_FORMAT_PARITY_NONE) {
		int i;
		char *b = buffer;
		for (i = 0; i < count; ++i) {
			int c, p = 0;
			c = buf[i];
			switch (portdata->iwa) {
			case UART_FORMAT_PARITY_ODD:
				p = !vizzini_parity[c];
				break;
			case UART_FORMAT_PARITY_EVEN:
				p = vizzini_parity[c];
				break;
			case UART_FORMAT_PARITY_1:
				p = 1;
				break;
			case UART_FORMAT_PARITY_0:
				p = 0;
				break;
			}
			*b++ = c;
			*b++ = p;
		}
	} else
#endif
		memcpy(buffer, buf, count);

	usb_fill_bulk_urb(urb, serial->dev,
			  usb_sndbulkpipe(serial->dev,
					  port->bulk_out_endpointAddress),
			  buffer, bufsize, vizzini_out_callback, port);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_err(&port->dev, "%s - usb_submit_urb(write bulk) failed with status = %d\n", __func__, status);
		count = status;
		goto error;
	}

	/* we are done with this urb, so let the host driver
	 * really free it when it is finished with it */
	usb_free_urb(urb);

	return count;
error:
	usb_free_urb(urb);
error_no_urb:
	kfree(buffer);
error_no_buffer:
	spin_lock_irqsave(&portdata->lock, flags);
	--portdata->outstanding_urbs;
	spin_unlock_irqrestore(&portdata->lock, flags);
	return count;
}

static void vizzini_in_callback(struct urb *urb)
{
	int endpoint = usb_pipeendpoint(urb->pipe);
	struct usb_serial_port *port = urb->context;
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	struct tty_struct *tty = port->port.tty;
	int preciseflags = portdata->preciseflags;
	char *transfer_buffer = urb->transfer_buffer;
	int length, room, have_extra_byte;
	int err;

	if (urb->status) {
		dev_dbg(&port->dev, "%s: nonzero status: %d on endpoint %02x.\n", __func__, urb->status, endpoint);
		return;
	}

#ifdef VIZZINI_IWA
	if (portdata->iwa != UART_FORMAT_PARITY_NONE)
		preciseflags = true;
#endif

	length = urb->actual_length;
	if (length == 0) {
		dev_dbg(&port->dev, "%s: empty read urb received\n", __func__);
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err)
			dev_err(&port->dev, "resubmit read urb failed. (%d)\n", err);
		return;
	}

	length = length + (portdata->have_extra_byte ? 1 : 0);
	have_extra_byte = (preciseflags && (length & 1));
	length = (preciseflags) ? (length / 2) : length;

	room = tty_buffer_request_room(tty, length);
	if (room != length)
		dev_dbg(&port->dev, "Not enough room in TTY buf, dropped %d chars.\n", length - room);

	if (room) {
		if (preciseflags) {
			char *dp = transfer_buffer;
			int i, ch, ch_flags;

			for (i = 0; i < room; ++i) {
				char tty_flag;

				if (i == 0) {
					if (portdata->have_extra_byte)
						ch = portdata->extra_byte;
					else
						ch = *dp++;
				} else {
					ch = *dp++;
				}
				ch_flags = *dp++;

#ifdef VIZZINI_IWA
				{
					int p;
					switch (portdata->iwa) {
					case UART_FORMAT_PARITY_ODD:
						p = !vizzini_parity[ch];
						break;
					case UART_FORMAT_PARITY_EVEN:
						p = vizzini_parity[ch];
						break;
					case UART_FORMAT_PARITY_1:
						p = 1;
						break;
					case UART_FORMAT_PARITY_0:
						p = 0;
						break;
					default:
						p = 0;
						break;
					}
					ch_flags ^= p;
				}
#endif
				if (ch_flags & RAMCTL_BUFFER_PARITY)
					tty_flag = TTY_PARITY;
				else if (ch_flags & RAMCTL_BUFFER_BREAK)
					tty_flag = TTY_BREAK;
				else if (ch_flags & RAMCTL_BUFFER_FRAME)
					tty_flag = TTY_FRAME;
				else if (ch_flags & RAMCTL_BUFFER_OVERRUN)
					tty_flag = TTY_OVERRUN;
				else
					tty_flag = TTY_NORMAL;

				tty_insert_flip_char(tty, ch, tty_flag);
			}
		} else {
			tty_insert_flip_string(tty, transfer_buffer, room);
		}

		tty_flip_buffer_push(tty);
	}

	portdata->have_extra_byte = have_extra_byte;
	if (have_extra_byte)
		portdata->extra_byte = transfer_buffer[urb->actual_length - 1];

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err)
		dev_err(&port->dev, "resubmit read urb failed. (%d)\n", err);
}

static void vizzini_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct vizzini_port_private *portdata = usb_get_serial_port_data(port);
	struct tty_struct *tty = port->port.tty;

	struct usb_cdc_notification *dr = urb->transfer_buffer;
	unsigned char *data;
	int newctrl;
	int status;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&port->dev, "urb shutting down with status: %d\n", urb->status);
		return;
	default:
		dev_dbg(&port->dev, "nonzero urb status received: %d\n", urb->status);
		goto exit;
	}

	data = (unsigned char *)(dr + 1);
	switch (dr->bNotificationType) {

	case USB_CDC_NOTIFY_NETWORK_CONNECTION:
		dev_dbg(&port->dev, "%s network\n", dr->wValue ? "connected to" : "disconnected from");
		break;

	case USB_CDC_NOTIFY_SERIAL_STATE:
		newctrl = le16_to_cpu(get_unaligned((__le16 *)data));

		if (!portdata->clocal && (portdata->ctrlin & ~newctrl & ACM_CTRL_DCD)) {
			dev_dbg(&port->dev, "calling hangup\n");
			tty_hangup(tty);
		}

		portdata->ctrlin = newctrl;

		dev_dbg(&port->dev, "input control lines: dcd%c dsr%c break%c ring%c framing%c parity%c overrun%c\n",
				   portdata->ctrlin & ACM_CTRL_DCD ? '+' : '-',
				   portdata->ctrlin & ACM_CTRL_DSR ? '+' : '-',
				   portdata->ctrlin & ACM_CTRL_BRK ? '+' : '-',
				   portdata->ctrlin & ACM_CTRL_RI  ? '+' : '-',
				   portdata->ctrlin & ACM_CTRL_FRAMING ? '+' : '-',
				   portdata->ctrlin & ACM_CTRL_PARITY ? '+' : '-',
				   portdata->ctrlin & ACM_CTRL_OVERRUN ? '+' : '-');
		break;

	default:
		dev_dbg(&port->dev, "unknown notification %d received: index %d len %d data0 %d data1 %d\n",
				   dr->bNotificationType, dr->wIndex,
				   dr->wLength, data[0], data[1]);
		break;
	}
exit:
	dev_dbg(&port->dev, "Resubmitting interrupt IN urb %p\n", urb);
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status)
		dev_err(&port->dev, "usb_submit_urb failed with result %d", status);
}

static int vizzini_open(struct tty_struct *tty_param, struct usb_serial_port *port)
{
	struct vizzini_port_private *portdata;
	struct usb_serial *serial = port->serial;
	struct tty_struct *tty = port->port.tty;
	int i;
	struct urb *urb;
	int result;

	portdata = usb_get_serial_port_data(port);

	acm_set_control(port, portdata->ctrlout = ACM_CTRL_DTR | ACM_CTRL_RTS);

	/* Reset low level data toggle and start reading from endpoints */
	for (i = 0; i < N_IN_URB; i++) {
		dev_dbg(&port->dev, "%s urb %d\n", __func__, i);

		urb = portdata->in_urbs[i];
		if (!urb)
			continue;
		if (urb->dev != serial->dev) {
			dev_dbg(&port->dev, "%s: dev %p != %p\n", __func__,
					   urb->dev, serial->dev);
			continue;
		}

		/*
		 * make sure endpoint data toggle is synchronized with the
		 * device
		 */
		/* dev_dbg(&port->dev, "%s clearing halt on %x\n", __func__, urb->pipe); */
		/* usb_clear_halt(urb->dev, urb->pipe); */

		dev_dbg(&port->dev, "%s submitting urb %p\n", __func__, urb);
		result = usb_submit_urb(urb, GFP_KERNEL);
		if (result) {
			dev_err(&port->dev, "submit urb %d failed (%d) %d\n",
				i, result, urb->transfer_buffer_length);
		}
	}

	tty->low_latency = 1;

	/* start up the interrupt endpoint if we have one */
	if (port->interrupt_in_urb) {
		result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
		if (result)
			dev_err(&port->dev, "submit irq_in urb failed %d\n",
				result);
	}
	return 0;
}

static void vizzini_close(struct usb_serial_port *port)
{
	int                              i;
	struct usb_serial               *serial = port->serial;
	struct vizzini_port_private     *portdata;
	struct tty_struct               *tty    = port->port.tty;

	portdata = usb_get_serial_port_data(port);

	acm_set_control(port, portdata->ctrlout = 0);

	if (serial->dev) {
		/* Stop reading/writing urbs */
		for (i = 0; i < N_IN_URB; i++)
			usb_kill_urb(portdata->in_urbs[i]);
	}

	usb_kill_urb(port->interrupt_in_urb);

	tty = NULL; /* FIXME */
}

static int vizzini_attach(struct usb_serial *serial)
{
	struct vizzini_serial_private   *serial_priv       = usb_get_serial_data(serial);
	struct usb_interface            *interface         = serial_priv->data_interface;
	struct usb_host_interface       *iface_desc;
	struct usb_endpoint_descriptor  *endpoint;
	struct usb_endpoint_descriptor  *bulk_in_endpoint  = NULL;
	struct usb_endpoint_descriptor  *bulk_out_endpoint = NULL;

	struct usb_serial_port          *port;
	struct vizzini_port_private     *portdata;
	struct urb                      *urb;
	int                              i, j;

	/* Assume that there's exactly one serial port. */
	port = serial->port[0];

	/* The usb_serial is now fully set up, but we want to make a
	 * couple of modifications.  Namely, it was configured based
	 * upon the control interface and not the data interface, so
	 * it has no notion of the bulk in and out endpoints.  So we
	 * essentially do some of the same allocations and
	 * configurations that the usb-serial core would have done if
	 * it had not made any faulty assumptions about the
	 * endpoints. */

	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(endpoint))
			bulk_in_endpoint = endpoint;

		if (usb_endpoint_is_bulk_out(endpoint))
			bulk_out_endpoint = endpoint;
	}

	if (!bulk_out_endpoint || !bulk_in_endpoint) {
		dev_dbg(&port->dev, "Missing endpoint!\n");
		return -EINVAL;
	}

	port->bulk_out_endpointAddress = bulk_out_endpoint->bEndpointAddress;
	port->bulk_in_endpointAddress = bulk_in_endpoint->bEndpointAddress;

	portdata = kzalloc(sizeof(*portdata), GFP_KERNEL);
	if (!portdata) {
		dev_dbg(&port->dev, "%s: kmalloc for vizzini_port_private (%d) failed!.\n",
				   __func__, i);
		return -ENOMEM;
	}
	spin_lock_init(&portdata->lock);
	for (j = 0; j < N_IN_URB; j++) {
		portdata->in_buffer[j] = kmalloc(IN_BUFLEN, GFP_KERNEL);
		if (!portdata->in_buffer[j]) {
			for (--j; j >= 0; j--)
				kfree(portdata->in_buffer[j]);
			kfree(portdata);
			return -ENOMEM;
		}
	}

	/* Bulk OUT endpoints 0x1..0x4 map to register blocks 0..3 */
	portdata->block = port->bulk_out_endpointAddress - 1;

	usb_set_serial_port_data(port, portdata);

	portdata->bcd_device = le16_to_cpu(serial->dev->descriptor.bcdDevice);
	if (vizzini_rev_a(port))
		dev_info(&port->dev, "Adapting to revA silicon\n");

	/* initialize the in urbs */
	for (j = 0; j < N_IN_URB; ++j) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (urb == NULL) {
			dev_dbg(&port->dev, "%s: alloc for in port failed.\n", __func__);
			continue;
		}
		/* Fill URB using supplied data. */
		dev_dbg(&port->dev, "Filling URB %p, EP=%d buf=%p len=%d\n", urb, port->bulk_in_endpointAddress, portdata->in_buffer[j], IN_BUFLEN);
		usb_fill_bulk_urb(urb, serial->dev,
				  usb_rcvbulkpipe(serial->dev,
						  port->bulk_in_endpointAddress),
				  portdata->in_buffer[j], IN_BUFLEN,
				  vizzini_in_callback, port);
		portdata->in_urbs[j] = urb;
	}

	return 0;
}

static void vizzini_serial_disconnect(struct usb_serial *serial)
{
	struct usb_serial_port          *port;
	struct vizzini_port_private     *portdata;
	int                              i, j;

	dev_dbg(&serial->dev->dev, "%s %p\n", __func__, serial);

	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		portdata = usb_get_serial_port_data(port);
		if (!portdata)
			continue;

		for (j = 0; j < N_IN_URB; j++) {
			usb_kill_urb(portdata->in_urbs[j]);
			usb_free_urb(portdata->in_urbs[j]);
		}
	}
}

static void vizzini_serial_release(struct usb_serial *serial)
{
	struct usb_serial_port          *port;
	struct vizzini_port_private     *portdata;
	int                              i, j;

	dev_dbg(&serial->dev->dev, "%s %p\n", __func__, serial);

	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		portdata = usb_get_serial_port_data(port);
		if (!portdata)
			continue;

		for (j = 0; j < N_IN_URB; j++)
			kfree(portdata->in_buffer[j]);

		kfree(portdata);
		usb_set_serial_port_data(port, NULL);
	}
}

static int vizzini_calc_num_ports(struct usb_serial *serial)
{
	return 1;
}

static int vizzini_probe(struct usb_serial *serial,
			 const struct usb_device_id *id)
{
	struct usb_interface *intf = serial->interface;
	unsigned char *buffer = intf->altsetting->extra;
	int buflen = intf->altsetting->extralen;
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_cdc_union_desc *union_header = NULL;
	struct usb_cdc_country_functional_desc  *cfd = NULL;
	int call_interface_num = -1;
	int data_interface_num;
	struct usb_interface *control_interface;
	struct usb_interface *data_interface;
	struct usb_endpoint_descriptor *epctrl;
	struct usb_endpoint_descriptor *epread;
	struct usb_endpoint_descriptor *epwrite;
	struct vizzini_serial_private *serial_priv;

	if (!buffer) {
		dev_err(&intf->dev, "Weird descriptor references\n");
		return -EINVAL;
	}

	if (!buflen) {
		if (intf->cur_altsetting->endpoint->extralen && intf->cur_altsetting->endpoint->extra) {
			dev_dbg(&intf->dev, "Seeking extra descriptors on endpoint\n");
			buflen = intf->cur_altsetting->endpoint->extralen;
			buffer = intf->cur_altsetting->endpoint->extra;
		} else {
			dev_err(&intf->dev, "Zero length descriptor references\n");
			return -EINVAL;
		}
	}

	while (buflen > 0) {
		if (buffer[1] != USB_DT_CS_INTERFACE) {
			dev_err(&intf->dev, "skipping garbage\n");
			goto next_desc;
		}

		switch (buffer[2]) {
		case USB_CDC_UNION_TYPE: /* we've found it */
			if (union_header) {
				dev_err(&intf->dev, "More than one union descriptor, skipping ...\n");
				goto next_desc;
			}
			union_header = (struct usb_cdc_union_desc *)buffer;
			break;
		case USB_CDC_COUNTRY_TYPE: /* export through sysfs */
			cfd = (struct usb_cdc_country_functional_desc *)buffer;
			break;
		case USB_CDC_HEADER_TYPE: /* maybe check version */
			break; /* for now we ignore it */
		case USB_CDC_CALL_MANAGEMENT_TYPE:
			call_interface_num = buffer[4];
			break;
		default:
			/* there are LOTS more CDC descriptors that
			 * could legitimately be found here.
			 */
			dev_dbg(&intf->dev, "Ignoring descriptor: type %02x, length %d\n", buffer[2], buffer[0]);
			break;
		}
next_desc:
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (!union_header) {
		if (call_interface_num > 0) {
			dev_dbg(&intf->dev, "No union descriptor, using call management descriptor\n");
			data_interface = usb_ifnum_to_if(usb_dev, (data_interface_num = call_interface_num));
			control_interface = intf;
		} else {
			dev_dbg(&intf->dev, "No union descriptor, giving up\n");
			return -ENODEV;
		}
	} else {
		control_interface = usb_ifnum_to_if(usb_dev, union_header->bMasterInterface0);
		data_interface    = usb_ifnum_to_if(usb_dev, (data_interface_num = union_header->bSlaveInterface0));
		if (!control_interface || !data_interface) {
			dev_dbg(&intf->dev, "no interfaces\n");
			return -ENODEV;
		}
	}

	if (data_interface_num != call_interface_num)
		dev_dbg(&intf->dev, "Separate call control interface. That is not fully supported.\n");

	/* workaround for switched interfaces */
	if (data_interface->cur_altsetting->desc.bInterfaceClass != CDC_DATA_INTERFACE_TYPE) {
		if (control_interface->cur_altsetting->desc.bInterfaceClass == CDC_DATA_INTERFACE_TYPE) {
			struct usb_interface *t;

			t = control_interface;
			control_interface = data_interface;
			data_interface = t;
		} else {
			return -EINVAL;
		}
	}

	/* Accept probe requests only for the control interface */
	if (intf != control_interface)
		return -ENODEV;

	if (usb_interface_claimed(data_interface)) { /* valid in this context */
		dev_dbg(&intf->dev, "The data interface isn't available\n");
		return -EBUSY;
	}

	if (data_interface->cur_altsetting->desc.bNumEndpoints < 2)
		return -EINVAL;

	epctrl  = &control_interface->cur_altsetting->endpoint[0].desc;
	epread  = &data_interface->cur_altsetting->endpoint[0].desc;
	epwrite = &data_interface->cur_altsetting->endpoint[1].desc;
	if (!usb_endpoint_dir_in(epread)) {
		struct usb_endpoint_descriptor *t;
		t   = epread;
		epread  = epwrite;
		epwrite = t;
	}

	/* The documentation suggests that we allocate private storage
	 * with the attach() entry point, but we can't allow the data
	 * interface to remain unclaimed until then; so we need
	 * somewhere to save the claimed interface now. */
	serial_priv = kzalloc(sizeof(struct vizzini_serial_private),
			      GFP_KERNEL);
	if (!serial_priv)
		goto alloc_fail;
	usb_set_serial_data(serial, serial_priv);

	//usb_driver_claim_interface(&vizzini_driver, data_interface, NULL);

	/* Don't set the data interface private data.  When we
	 * disconnect we test this field against NULL to discover
	 * whether we're dealing with the control or data
	 * interface. */
	serial_priv->data_interface = data_interface;

	return 0;

alloc_fail:
	return -ENOMEM;
}

static struct usb_serial_driver vizzini_device = {
	.driver = {
		.owner =    THIS_MODULE,
		.name =     "vizzini",
	},
	.description =		"Vizzini USB serial port",
	.id_table =		id_table,
	.calc_num_ports	=	vizzini_calc_num_ports,
	.probe =		vizzini_probe,
	.open =			vizzini_open,
	.close =		vizzini_close,
	.write =		vizzini_write,
	.write_room =		vizzini_write_room,
	.ioctl =		vizzini_ioctl,
	.set_termios =		vizzini_set_termios,
	.break_ctl =		vizzini_break_ctl,
	.tiocmget =		vizzini_tiocmget,
	.tiocmset =		vizzini_tiocmset,
	.attach =		vizzini_attach,
	.disconnect =		vizzini_serial_disconnect,
	.release =		vizzini_serial_release,
	.read_int_callback =	vizzini_int_callback,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&vizzini_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
