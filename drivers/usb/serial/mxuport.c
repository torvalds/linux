// SPDX-License-Identifier: GPL-2.0+
/*
 *	mxuport.c - MOXA UPort series driver
 *
 *	Copyright (c) 2006 Moxa Technologies Co., Ltd.
 *	Copyright (c) 2013 Andrew Lunn <andrew@lunn.ch>
 *
 *	Supports the following Moxa USB to serial converters:
 *
 *	UPort G1:
 *		UPort 1250, UPort 1250I
 *		UPort 1410, UPort 1450, UPort 1450I
 *		UPort 1610-8, UPort 1650-8
 *		UPort 1610-16, UPort 1650-16
 *	UPort G2:
 *		UPort 1250-G2, UPort 1250I-G2
 *		UPort 1410-G2, UPort 1450-G2, UPort 1450I-G2
 *		UPort 1610-8-G2, UPort 1650-8-G2
 *		UPort 1650-8-G2-Hub, UPort 1650I-8-G2
 *	Platform UART:
 *		MU250U, MU450U, MU850U
 *		MUx50U-3, MUx50U-5, MUx50U-6, MUx50U-7
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/unaligned.h>

/* Definitions for the vendor ID and device ID */
#define MX_USBSERIAL_VID	0x110A
#define MX_UPORT1250_PID	0x1250
#define MX_UPORT1250I_PID	0x1251
#define MX_UPORT1410_PID	0x1410
#define MX_UPORT1450_PID	0x1450
#define MX_UPORT1450I_PID	0x1451
#define MX_UPORT1610_8_PID	0x1618
#define MX_UPORT1650_8_PID	0x1658
#define MX_UPORT1610_16_PID	0x1613
#define MX_UPORT1650_16_PID	0x1653

#define MX_UPORT1250_G2_PID		0x1252
#define MX_UPORT1250I_G2_PID		0x1253
#define MX_UPORT1410_G2_PID		0x1411
#define MX_UPORT1450_G2_PID		0x1452
#define MX_UPORT1450I_G2_PID		0x1453
#define MX_UPORT1610_8_G2_PID		0x1619
#define MX_UPORT1650_8_G2_PID		0x1659
#define MX_UPORT1650_8_G2_HUB_PID	0x165a
#define MX_UPORT1650I_8_G2_PID		0x165b

#define MX_MU250U_PID		0x0250
#define MX_MU450U_PID		0x0450
#define MX_MU850U_PID		0x0850
#define MX_MUX50U_6_PID		0x7002
#define MX_MUX50U_3_PID		0x7003
#define MX_MUX50U_5_PID		0x7004
#define MX_MUX50U_7_PID		0x7005

/* Definitions for USB info */
#define HEADER_SIZE		4
#define EVENT_LENGTH		8
#define DOWN_BLOCK_SIZE		64

/* Definitions for firmware info */
#define MX_FW_VER_BYTE1_OFF		0x20
#define MX_FW_VER_BYTE2_OFF		0x24
#define MX_FW_VER_BYTE3_OFF		0x28
#define MX_MUX50U_FW_VER_BYTE1_OFF	0x86
#define MX_MUX50U_FW_VER_BYTE2_OFF	0x88
#define MX_MUX50U_FW_VER_BYTE3_OFF	0x8a

#define MX_SYS_REG_REMAP_OFF		0x20
#define MX_PWR_REMAP_MASK		0x03
#define MX_REMAP_TO_SRAM		0x02

struct mxuport_fw_version {
	u8 major;
	u8 minor;
	u8 build;
	u32 value;
};

static const u16 mxuport_fw_ver_offsets[] = {
	MX_FW_VER_BYTE1_OFF,
	MX_FW_VER_BYTE2_OFF,
	MX_FW_VER_BYTE3_OFF,
};

static const u16 mxuport_mux50u_fw_ver_offsets[] = {
	MX_MUX50U_FW_VER_BYTE1_OFF,
	MX_MUX50U_FW_VER_BYTE2_OFF,
	MX_MUX50U_FW_VER_BYTE3_OFF,
};

/* Definitions for USB vendor request */
#define RQ_VENDOR_NONE			0x00
#define RQ_VENDOR_SET_BAUD		0x01 /* Set baud rate */
#define RQ_VENDOR_SET_LINE		0x02 /* Set line status */
#define RQ_VENDOR_SET_CHARS		0x03 /* Set Xon/Xoff chars */
#define RQ_VENDOR_SET_RTS		0x04 /* Set RTS */
#define RQ_VENDOR_SET_DTR		0x05 /* Set DTR */
#define RQ_VENDOR_SET_XONXOFF		0x06 /* Set auto Xon/Xoff */
#define RQ_VENDOR_SET_RX_HOST_EN	0x07 /* Set RX host enable */
#define RQ_VENDOR_SET_OPEN		0x08 /* Set open/close port */
#define RQ_VENDOR_PURGE			0x09 /* Purge Rx/Tx buffer */
#define RQ_VENDOR_SET_MCR		0x0A /* Set MCR register */
#define RQ_VENDOR_SET_BREAK		0x0B /* Set Break signal */

#define RQ_VENDOR_START_FW_DOWN		0x0C /* Start firmware download */
#define RQ_VENDOR_STOP_FW_DOWN		0x0D /* Stop firmware download */
#define RQ_VENDOR_QUERY_FW_READY	0x0E /* Query if new firmware ready */

#define RQ_VENDOR_SET_FIFO_DISABLE	0x0F /* Set fifo disable */
#define RQ_VENDOR_SET_INTERFACE		0x10 /* Set interface */
#define RQ_VENDOR_SET_HIGH_PERFOR	0x11 /* Set hi-performance */

#define RQ_VENDOR_ERASE_BLOCK		0x12 /* Erase flash block */
#define RQ_VENDOR_WRITE_PAGE		0x13 /* Write flash page */
#define RQ_VENDOR_PREPARE_WRITE		0x14 /* Prepare write flash */
#define RQ_VENDOR_CONFIRM_WRITE		0x15 /* Confirm write flash */
#define RQ_VENDOR_LOCATE		0x16 /* Locate the device */

#define RQ_VENDOR_START_ROM_DOWN	0x17 /* Start firmware download */
#define RQ_VENDOR_ROM_DATA		0x18 /* Rom file data */
#define RQ_VENDOR_STOP_ROM_DOWN		0x19 /* Stop firmware download */
#define RQ_VENDOR_FW_DATA		0x20 /* Firmware data */

#define RQ_VENDOR_RESET_DEVICE		0x23 /* Try to reset the device */
#define RQ_VENDOR_QUERY_FW_CONFIG	0x24

#define RQ_VENDOR_GET_SYS_REG		0x34

#define RQ_VENDOR_GET_VERSION		0x81 /* Get firmware version */
#define RQ_VENDOR_GET_PAGE		0x82 /* Read flash page */
#define RQ_VENDOR_GET_ROM_PROC		0x83 /* Get ROM process state */

#define RQ_VENDOR_GET_INQUEUE		0x84 /* Data in input buffer */
#define RQ_VENDOR_GET_OUTQUEUE		0x85 /* Data in output buffer */

#define RQ_VENDOR_GET_MSR		0x86 /* Get modem status register */

/* Definitions for UPort event type */
#define UPORT_EVENT_NONE		0 /* None */
#define UPORT_EVENT_TXBUF_THRESHOLD	1 /* Tx buffer threshold */
#define UPORT_EVENT_SEND_NEXT		2 /* Send next */
#define UPORT_EVENT_MSR			3 /* Modem status */
#define UPORT_EVENT_LSR			4 /* Line status */
#define UPORT_EVENT_MCR			5 /* Modem control */

#define UPORT_REQUEST_SEND_NEXT		0x80

/* Definitions for serial event type */
#define SERIAL_EV_CTS			0x0008	/* CTS changed state */
#define SERIAL_EV_DSR			0x0010	/* DSR changed state */
#define SERIAL_EV_RLSD			0x0020	/* RLSD changed state */

/* Definitions for modem control event type */
#define SERIAL_EV_XOFF			0x40	/* XOFF received */

/* Definitions for line control of communication */
#define MX_WORDLENGTH_5			5
#define MX_WORDLENGTH_6			6
#define MX_WORDLENGTH_7			7
#define MX_WORDLENGTH_8			8

#define MX_PARITY_NONE			0
#define MX_PARITY_ODD			1
#define MX_PARITY_EVEN			2
#define MX_PARITY_MARK			3
#define MX_PARITY_SPACE			4

#define MX_STOP_BITS_1			0
#define MX_STOP_BITS_1_5		1
#define MX_STOP_BITS_2			2

#define MX_RTS_DISABLE			0x0
#define MX_RTS_ENABLE			0x1
#define MX_RTS_HW			0x2
#define MX_RTS_NO_CHANGE		0x3 /* Flag, not valid register value*/

#define MX_INT_RS232			0
#define MX_INT_2W_RS485			1
#define MX_INT_RS422			2
#define MX_INT_4W_RS485			3

/* Definitions for holding reason */
#define MX_WAIT_FOR_CTS			0x0001
#define MX_WAIT_FOR_DSR			0x0002
#define MX_WAIT_FOR_DCD			0x0004
#define MX_WAIT_FOR_XON			0x0008
#define MX_WAIT_FOR_START_TX		0x0010
#define MX_WAIT_FOR_UNTHROTTLE		0x0020
#define MX_WAIT_FOR_LOW_WATER		0x0040
#define MX_WAIT_FOR_SEND_NEXT		0x0080

/* This structure holds all of the local port information */
struct mxuport_port {
	u32 sent_payload;
	u8 hold_reason;
	u8 mcr_state;		/* Last MCR state */
	u8 msr_state;		/* Last MSR state */
	struct mutex mutex;	/* Protects mcr_state */
	spinlock_t spinlock;	/* Protects msr_state */
};

/* Encode number of ports (2..16 or undefined) */
#define MX_PORTS_MASK			GENMASK(3, 0)
#define MX_PORTS_OFFSET			1
#define MX_PORTS(n)			(((n) - MX_PORTS_OFFSET) & MX_PORTS_MASK)

#define MX_FAMILY_MASK			GENMASK(5, 4)
#define MX_UPORT_G1			0
#define MX_UPORT_G2			BIT(4)
#define MX_PLATFORM_UART		BIT(5)
#define MX_FAMILY(info)			((info) & MX_FAMILY_MASK)
#define MX_DEVICE_INFO(ports, family)	(MX_PORTS(ports) | (family))

#define MX_DEVICE(pid, ports, family)				\
	{ USB_DEVICE(MX_USBSERIAL_VID, pid),			\
	  .driver_info = MX_DEVICE_INFO(ports, family) }

/* Table of devices that work with this driver */
static const struct usb_device_id mxuport_idtable[] = {
	MX_DEVICE(MX_UPORT1250_PID,	     2, MX_UPORT_G1),
	MX_DEVICE(MX_UPORT1250I_PID,	     2, MX_UPORT_G1),
	MX_DEVICE(MX_UPORT1410_PID,	     4, MX_UPORT_G1),
	MX_DEVICE(MX_UPORT1450_PID,	     4, MX_UPORT_G1),
	MX_DEVICE(MX_UPORT1450I_PID,	     4, MX_UPORT_G1),
	MX_DEVICE(MX_UPORT1610_8_PID,	     8, MX_UPORT_G1),
	MX_DEVICE(MX_UPORT1650_8_PID,	     8, MX_UPORT_G1),
	MX_DEVICE(MX_UPORT1610_16_PID,	    16, MX_UPORT_G1),
	MX_DEVICE(MX_UPORT1650_16_PID,	    16, MX_UPORT_G1),
	MX_DEVICE(MX_UPORT1250_G2_PID,	     2, MX_UPORT_G2),
	MX_DEVICE(MX_UPORT1250I_G2_PID,	     2, MX_UPORT_G2),
	MX_DEVICE(MX_UPORT1410_G2_PID,	     4, MX_UPORT_G2),
	MX_DEVICE(MX_UPORT1450_G2_PID,	     4, MX_UPORT_G2),
	MX_DEVICE(MX_UPORT1450I_G2_PID,	     4, MX_UPORT_G2),
	MX_DEVICE(MX_UPORT1610_8_G2_PID,     8, MX_UPORT_G2),
	MX_DEVICE(MX_UPORT1650_8_G2_PID,     8, MX_UPORT_G2),
	MX_DEVICE(MX_UPORT1650_8_G2_HUB_PID, 8, MX_UPORT_G2),
	MX_DEVICE(MX_UPORT1650I_8_G2_PID,    8, MX_UPORT_G2),
	MX_DEVICE(MX_MU250U_PID,	     2, MX_PLATFORM_UART),
	MX_DEVICE(MX_MU450U_PID,	     4, MX_PLATFORM_UART),
	MX_DEVICE(MX_MU850U_PID,	     8, MX_PLATFORM_UART),
	MX_DEVICE(MX_MUX50U_6_PID,	     6, MX_PLATFORM_UART),
	MX_DEVICE(MX_MUX50U_3_PID,	     3, MX_PLATFORM_UART),
	MX_DEVICE(MX_MUX50U_5_PID,	     5, MX_PLATFORM_UART),
	MX_DEVICE(MX_MUX50U_7_PID,	     7, MX_PLATFORM_UART),
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, mxuport_idtable);

static bool mxuport_write_ready(struct usb_serial_port *port)
{
	struct mxuport_port *mxport = usb_get_serial_port_data(port);

	return !(mxport->hold_reason & MX_WAIT_FOR_SEND_NEXT);
}

/*
 * Add a four byte header containing the port number and the number of
 * bytes of data in the message. Return the number of bytes in the
 * buffer.
 */
static int mxuport_prepare_write_buffer(struct usb_serial_port *port,
					void *dest, size_t size)
{
	struct mxuport_port *mxport = usb_get_serial_port_data(port);
	bool request_send_next;
	unsigned long flags;
	u8 *buf = dest;
	int count;

	spin_lock_irqsave(&port->lock, flags);
	count = kfifo_out(&port->write_fifo, buf + HEADER_SIZE, size - HEADER_SIZE);
	mxport->sent_payload += count;
	request_send_next = mxport->sent_payload >= port->bulk_out_size;
	if (request_send_next)
		mxport->hold_reason |= MX_WAIT_FOR_SEND_NEXT;
	spin_unlock_irqrestore(&port->lock, flags);

	put_unaligned_be16(port->port_number, buf);
	put_unaligned_be16(count, buf + 2);

	if (request_send_next)
		buf[0] |= UPORT_REQUEST_SEND_NEXT;

	dev_dbg(&port->dev, "%s - size %zd count %d\n", __func__,
		size, count);

	return count + HEADER_SIZE;
}

static void mxuport_write_rollback(struct usb_serial_port *port, int count)
{
	struct mxuport_port *mxport = usb_get_serial_port_data(port);

	mxport->sent_payload -= count - HEADER_SIZE;
	mxport->hold_reason &= ~MX_WAIT_FOR_SEND_NEXT;
}

/* Read the given buffer in from the control pipe. */
static int mxuport_recv_ctrl_urb(struct usb_serial *serial,
				 u8 request, u16 value, u16 index,
				 u8 *data, size_t size)
{
	int status;

	status = usb_control_msg(serial->dev,
				 usb_rcvctrlpipe(serial->dev, 0),
				 request,
				 (USB_DIR_IN | USB_TYPE_VENDOR |
				  USB_RECIP_DEVICE), value, index,
				 data, size,
				 USB_CTRL_GET_TIMEOUT);
	if (status < 0) {
		dev_err(&serial->interface->dev,
			"%s - usb_control_msg failed (%d)\n",
			__func__, status);
		return status;
	}

	if (status != size) {
		dev_err(&serial->interface->dev,
			"%s - short read (%d / %zd)\n",
			__func__, status, size);
		return -EIO;
	}

	return status;
}

/* Write the given buffer out to the control pipe.  */
static int mxuport_send_ctrl_data_urb(struct usb_serial *serial,
				      u8 request,
				      u16 value, u16 index,
				      u8 *data, size_t size)
{
	int status;

	status = usb_control_msg(serial->dev,
				 usb_sndctrlpipe(serial->dev, 0),
				 request,
				 (USB_DIR_OUT | USB_TYPE_VENDOR |
				  USB_RECIP_DEVICE), value, index,
				 data, size,
				 USB_CTRL_SET_TIMEOUT);
	if (status < 0) {
		dev_err(&serial->interface->dev,
			"%s - usb_control_msg failed (%d)\n",
			__func__, status);
		return status;
	}

	return 0;
}

/* Send a vendor request without any data */
static int mxuport_send_ctrl_urb(struct usb_serial *serial,
				 u8 request, u16 value, u16 index)
{
	return mxuport_send_ctrl_data_urb(serial, request, value, index,
					  NULL, 0);
}

/*
 * mxuport_throttle - throttle function of driver
 *
 * This function is called by the tty driver when it wants to stop the
 * data being read from the port. Since all the data comes over one
 * bulk in endpoint, we cannot stop submitting urbs by setting
 * port->throttle. Instead tell the device to stop sending us data for
 * the port.
 */
static void mxuport_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;

	dev_dbg(&port->dev, "%s\n", __func__);

	mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_RX_HOST_EN,
			      0, port->port_number);
}

/*
 * mxuport_unthrottle - unthrottle function of driver
 *
 * This function is called by the tty driver when it wants to resume
 * the data being read from the port. Tell the device it can resume
 * sending us received data from the port.
 */
static void mxuport_unthrottle(struct tty_struct *tty)
{

	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;

	dev_dbg(&port->dev, "%s\n", __func__);

	mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_RX_HOST_EN,
			      1, port->port_number);
}

/*
 * Processes one chunk of data received for a port.  Mostly a copy of
 * usb_serial_generic_process_read_urb().
 */
static void mxuport_process_read_urb_data(struct usb_serial_port *port,
					  char *data, int size)
{
	int i;

	if (port->sysrq) {
		for (i = 0; i < size; i++, data++) {
			if (!usb_serial_handle_sysrq_char(port, *data))
				tty_insert_flip_char(&port->port, *data,
						     TTY_NORMAL);
		}
	} else {
		tty_insert_flip_string(&port->port, data, size);
	}
	tty_flip_buffer_push(&port->port);
}

static void mxuport_msr_event(struct usb_serial_port *port, u8 buf[4])
{
	struct mxuport_port *mxport = usb_get_serial_port_data(port);
	u8 rcv_msr_hold = buf[2] & 0xF0;
	u16 rcv_msr_event = get_unaligned_be16(buf);
	unsigned long flags;

	if (rcv_msr_event == 0)
		return;

	/* Update MSR status */
	spin_lock_irqsave(&mxport->spinlock, flags);

	dev_dbg(&port->dev, "%s - current MSR status = 0x%x\n",
		__func__, mxport->msr_state);

	if (rcv_msr_hold & UART_MSR_CTS) {
		mxport->msr_state |= UART_MSR_CTS;
		dev_dbg(&port->dev, "%s - CTS high\n", __func__);
	} else {
		mxport->msr_state &= ~UART_MSR_CTS;
		dev_dbg(&port->dev, "%s - CTS low\n", __func__);
	}

	if (rcv_msr_hold & UART_MSR_DSR) {
		mxport->msr_state |= UART_MSR_DSR;
		dev_dbg(&port->dev, "%s - DSR high\n", __func__);
	} else {
		mxport->msr_state &= ~UART_MSR_DSR;
		dev_dbg(&port->dev, "%s - DSR low\n", __func__);
	}

	if (rcv_msr_hold & UART_MSR_DCD) {
		mxport->msr_state |= UART_MSR_DCD;
		dev_dbg(&port->dev, "%s - DCD high\n", __func__);
	} else {
		mxport->msr_state &= ~UART_MSR_DCD;
		dev_dbg(&port->dev, "%s - DCD low\n", __func__);
	}
	spin_unlock_irqrestore(&mxport->spinlock, flags);

	if (rcv_msr_event &
	    (SERIAL_EV_CTS | SERIAL_EV_DSR | SERIAL_EV_RLSD)) {

		if (rcv_msr_event & SERIAL_EV_CTS) {
			port->icount.cts++;
			dev_dbg(&port->dev, "%s - CTS change\n", __func__);
		}

		if (rcv_msr_event & SERIAL_EV_DSR) {
			port->icount.dsr++;
			dev_dbg(&port->dev, "%s - DSR change\n", __func__);
		}

		if (rcv_msr_event & SERIAL_EV_RLSD) {
			port->icount.dcd++;
			dev_dbg(&port->dev, "%s - DCD change\n", __func__);
		}
		wake_up_interruptible(&port->port.delta_msr_wait);
	}
}

static void mxuport_lsr_event(struct usb_serial_port *port, u8 buf[4])
{
	u8 lsr_event = buf[2];

	if (lsr_event & UART_LSR_BI) {
		port->icount.brk++;
		dev_dbg(&port->dev, "%s - break error\n", __func__);
	}

	if (lsr_event & UART_LSR_FE) {
		port->icount.frame++;
		dev_dbg(&port->dev, "%s - frame error\n", __func__);
	}

	if (lsr_event & UART_LSR_PE) {
		port->icount.parity++;
		dev_dbg(&port->dev, "%s - parity error\n", __func__);
	}

	if (lsr_event & UART_LSR_OE) {
		port->icount.overrun++;
		dev_dbg(&port->dev, "%s - overrun error\n", __func__);
	}
}

/*
 * When something interesting happens, modem control lines XON/XOFF
 * etc, the device sends an event. Process these events.
 */
static void mxuport_process_read_urb_event(struct usb_serial_port *port,
					   u8 buf[4], u32 event)
{
	struct mxuport_port *mxport = usb_get_serial_port_data(port);
	bool resume_tx = false;
	unsigned long flags;

	dev_dbg(&port->dev, "%s - receive event : %04x\n", __func__, event);

	switch (event) {
	case UPORT_EVENT_SEND_NEXT:
		spin_lock_irqsave(&port->lock, flags);
		if (mxport->hold_reason & MX_WAIT_FOR_SEND_NEXT) {
			mxport->hold_reason &= ~MX_WAIT_FOR_SEND_NEXT;
			mxport->sent_payload = 0;
			resume_tx = true;
		}
		spin_unlock_irqrestore(&port->lock, flags);

		if (resume_tx)
			usb_serial_generic_write_start(port, GFP_ATOMIC);
		break;
	case UPORT_EVENT_MSR:
		mxuport_msr_event(port, buf);
		break;
	case UPORT_EVENT_LSR:
		mxuport_lsr_event(port, buf);
		break;
	case UPORT_EVENT_MCR:
		/*
		 * Event to indicate a change in XON/XOFF from the
		 * peer.  Currently not used. We just continue
		 * sending the device data and it will buffer it if
		 * needed. This event could be used for flow control
		 * between the host and the device.
		 */
		break;
	default:
		dev_dbg(&port->dev, "Unexpected event\n");
		break;
	}
}

/*
 * One URB can contain data for multiple ports. Demultiplex the data,
 * checking the port exists, is opened and the message is valid.
 */
static void mxuport_process_read_urb_demux_data(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct usb_serial *serial = port->serial;
	u8 *data = urb->transfer_buffer;
	u8 *end = data + urb->actual_length;
	struct usb_serial_port *demux_port;
	u8 *ch;
	u16 rcv_port;
	u16 rcv_len;

	while (data < end) {
		if (data + HEADER_SIZE > end) {
			dev_warn(&port->dev, "%s - message with short header\n",
				 __func__);
			return;
		}

		rcv_port = get_unaligned_be16(data);
		if (rcv_port >= serial->num_ports) {
			dev_warn(&port->dev, "%s - message for invalid port\n",
				 __func__);
			return;
		}

		demux_port = serial->port[rcv_port];
		rcv_len = get_unaligned_be16(data + 2);
		if (!rcv_len || data + HEADER_SIZE + rcv_len > end) {
			dev_warn(&port->dev, "%s - short data\n", __func__);
			return;
		}

		if (tty_port_initialized(&demux_port->port)) {
			ch = data + HEADER_SIZE;
			mxuport_process_read_urb_data(demux_port, ch, rcv_len);
		} else {
			dev_dbg(&demux_port->dev, "%s - data for closed port\n",
				__func__);
		}
		data += HEADER_SIZE + rcv_len;
	}
}

/*
 * One URB can contain events for multiple ports. Demultiplex the event,
 * checking the port exists, and is opened.
 */
static void mxuport_process_read_urb_demux_event(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct usb_serial *serial = port->serial;
	u8 *data = urb->transfer_buffer;
	u8 *end = data + urb->actual_length;
	struct usb_serial_port *demux_port;
	u8 *ch;
	u16 rcv_port;
	u16 rcv_event;

	while (data < end) {
		if (data + EVENT_LENGTH > end) {
			dev_warn(&port->dev, "%s - message with short event\n",
				 __func__);
			return;
		}

		rcv_port = get_unaligned_be16(data);
		if (rcv_port >= serial->num_ports) {
			dev_warn(&port->dev, "%s - message for invalid port\n",
				 __func__);
			return;
		}

		demux_port = serial->port[rcv_port];
		if (tty_port_initialized(&demux_port->port)) {
			ch = data + HEADER_SIZE;
			rcv_event = get_unaligned_be16(data + 2);
			mxuport_process_read_urb_event(demux_port, ch,
						       rcv_event);
		} else {
			dev_dbg(&demux_port->dev,
				"%s - event for closed port\n", __func__);
		}
		data += EVENT_LENGTH;
	}
}

/*
 * This is called when we have received data on the bulk in
 * endpoint. Depending on which port it was received on, it can
 * contain serial data or events.
 */
static void mxuport_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct usb_serial *serial = port->serial;

	if (port == serial->port[0])
		mxuport_process_read_urb_demux_data(urb);

	if (port == serial->port[1])
		mxuport_process_read_urb_demux_event(urb);
}

/*
 * Ask the device how many bytes it has queued to be sent out. If
 * there are none, return true.
 */
static bool mxuport_tx_empty(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	bool is_empty = true;
	u32 txlen;
	u8 *len_buf;
	int err;

	len_buf = kzalloc(4, GFP_KERNEL);
	if (!len_buf)
		goto out;

	err = mxuport_recv_ctrl_urb(serial, RQ_VENDOR_GET_OUTQUEUE, 0,
				    port->port_number, len_buf, 4);
	if (err < 0)
		goto out;

	txlen = get_unaligned_be32(len_buf);
	dev_dbg(&port->dev, "%s - tx len = %u\n", __func__, txlen);

	if (txlen != 0)
		is_empty = false;

out:
	kfree(len_buf);
	return is_empty;
}

static int mxuport_set_mcr(struct usb_serial_port *port, u8 mcr_state)
{
	struct usb_serial *serial = port->serial;
	int err;

	dev_dbg(&port->dev, "%s - %02x\n", __func__, mcr_state);

	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_MCR,
				    mcr_state, port->port_number);
	if (err)
		dev_err(&port->dev, "%s - failed to change MCR\n", __func__);

	return err;
}

static int mxuport_set_dtr(struct usb_serial_port *port, int on)
{
	struct mxuport_port *mxport = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	int err;

	mutex_lock(&mxport->mutex);

	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_DTR,
				    !!on, port->port_number);
	if (!err) {
		if (on)
			mxport->mcr_state |= UART_MCR_DTR;
		else
			mxport->mcr_state &= ~UART_MCR_DTR;
	}

	mutex_unlock(&mxport->mutex);

	return err;
}

static int mxuport_set_rts(struct usb_serial_port *port, u8 state)
{
	struct mxuport_port *mxport = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	int err;
	u8 mcr_state;

	mutex_lock(&mxport->mutex);
	mcr_state = mxport->mcr_state;

	switch (state) {
	case MX_RTS_DISABLE:
		mcr_state &= ~UART_MCR_RTS;
		break;
	case MX_RTS_ENABLE:
		mcr_state |= UART_MCR_RTS;
		break;
	case MX_RTS_HW:
		/*
		 * Do not update mxport->mcr_state when doing hardware
		 * flow control.
		 */
		break;
	default:
		/*
		 * Should not happen, but somebody might try passing
		 * MX_RTS_NO_CHANGE, which is not valid.
		 */
		err = -EINVAL;
		goto out;
	}
	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_RTS,
				    state, port->port_number);
	if (!err)
		mxport->mcr_state = mcr_state;

out:
	mutex_unlock(&mxport->mutex);

	return err;
}

static void mxuport_dtr_rts(struct usb_serial_port *port, int on)
{
	struct mxuport_port *mxport = usb_get_serial_port_data(port);
	u8 mcr_state;
	int err;

	mutex_lock(&mxport->mutex);
	mcr_state = mxport->mcr_state;

	if (on)
		mcr_state |= (UART_MCR_RTS | UART_MCR_DTR);
	else
		mcr_state &= ~(UART_MCR_RTS | UART_MCR_DTR);

	err = mxuport_set_mcr(port, mcr_state);
	if (!err)
		mxport->mcr_state = mcr_state;

	mutex_unlock(&mxport->mutex);
}

static int mxuport_tiocmset(struct tty_struct *tty, unsigned int set,
			    unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mxuport_port *mxport = usb_get_serial_port_data(port);
	int err;
	u8 mcr_state;

	mutex_lock(&mxport->mutex);
	mcr_state = mxport->mcr_state;

	if (set & TIOCM_RTS)
		mcr_state |= UART_MCR_RTS;

	if (set & TIOCM_DTR)
		mcr_state |= UART_MCR_DTR;

	if (clear & TIOCM_RTS)
		mcr_state &= ~UART_MCR_RTS;

	if (clear & TIOCM_DTR)
		mcr_state &= ~UART_MCR_DTR;

	err = mxuport_set_mcr(port, mcr_state);
	if (!err)
		mxport->mcr_state = mcr_state;

	mutex_unlock(&mxport->mutex);

	return err;
}

static int mxuport_tiocmget(struct tty_struct *tty)
{
	struct mxuport_port *mxport;
	struct usb_serial_port *port = tty->driver_data;
	unsigned int result;
	unsigned long flags;
	unsigned int msr;
	unsigned int mcr;

	mxport = usb_get_serial_port_data(port);

	mutex_lock(&mxport->mutex);
	spin_lock_irqsave(&mxport->spinlock, flags);

	msr = mxport->msr_state;
	mcr = mxport->mcr_state;

	spin_unlock_irqrestore(&mxport->spinlock, flags);
	mutex_unlock(&mxport->mutex);

	result = (((mcr & UART_MCR_DTR) ? TIOCM_DTR : 0) |	/* 0x002 */
		  ((mcr & UART_MCR_RTS) ? TIOCM_RTS : 0) |	/* 0x004 */
		  ((msr & UART_MSR_CTS) ? TIOCM_CTS : 0) |	/* 0x020 */
		  ((msr & UART_MSR_DCD) ? TIOCM_CAR : 0) |	/* 0x040 */
		  ((msr & UART_MSR_RI) ? TIOCM_RI : 0) |	/* 0x080 */
		  ((msr & UART_MSR_DSR) ? TIOCM_DSR : 0));	/* 0x100 */

	dev_dbg(&port->dev, "%s - 0x%04x\n", __func__, result);

	return result;
}

static int mxuport_set_termios_flow(struct tty_struct *tty,
				    const struct ktermios *old_termios,
				    struct usb_serial_port *port,
				    struct usb_serial *serial)
{
	u8 xon = START_CHAR(tty);
	u8 xoff = STOP_CHAR(tty);
	int enable;
	int err;
	u8 *buf;
	u8 rts;

	buf = kmalloc(2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* S/W flow control settings */
	if (I_IXOFF(tty) || I_IXON(tty)) {
		enable = 1;
		buf[0] = xon;
		buf[1] = xoff;

		err = mxuport_send_ctrl_data_urb(serial, RQ_VENDOR_SET_CHARS,
						 0, port->port_number,
						 buf, 2);
		if (err)
			goto out;

		dev_dbg(&port->dev, "%s - XON = 0x%02x, XOFF = 0x%02x\n",
			__func__, xon, xoff);
	} else {
		enable = 0;
	}

	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_XONXOFF,
				    enable, port->port_number);
	if (err)
		goto out;

	rts = MX_RTS_NO_CHANGE;

	/* H/W flow control settings */
	if (!old_termios ||
	    C_CRTSCTS(tty) != (old_termios->c_cflag & CRTSCTS)) {
		if (C_CRTSCTS(tty))
			rts = MX_RTS_HW;
		else
			rts = MX_RTS_ENABLE;
	}

	if (C_BAUD(tty)) {
		if (old_termios && (old_termios->c_cflag & CBAUD) == B0) {
			/* Raise DTR and RTS */
			if (C_CRTSCTS(tty))
				rts = MX_RTS_HW;
			else
				rts = MX_RTS_ENABLE;
			mxuport_set_dtr(port, 1);
		}
	} else {
		/* Drop DTR and RTS */
		rts = MX_RTS_DISABLE;
		mxuport_set_dtr(port, 0);
	}

	if (rts != MX_RTS_NO_CHANGE)
		err = mxuport_set_rts(port, rts);

out:
	kfree(buf);
	return err;
}

static void mxuport_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				const struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	u8 *buf;
	u8 data_bits;
	u8 stop_bits;
	u8 parity;
	int baud;
	int err;

	if (old_termios &&
	    !tty_termios_hw_change(&tty->termios, old_termios) &&
	    tty->termios.c_iflag == old_termios->c_iflag) {
		dev_dbg(&port->dev, "%s - nothing to change\n", __func__);
		return;
	}

	buf = kmalloc(4, GFP_KERNEL);
	if (!buf)
		return;

	/* Set data bit of termios */
	switch (C_CSIZE(tty)) {
	case CS5:
		data_bits = MX_WORDLENGTH_5;
		break;
	case CS6:
		data_bits = MX_WORDLENGTH_6;
		break;
	case CS7:
		data_bits = MX_WORDLENGTH_7;
		break;
	case CS8:
	default:
		data_bits = MX_WORDLENGTH_8;
		break;
	}

	/* Set parity of termios */
	if (C_PARENB(tty)) {
		if (C_CMSPAR(tty)) {
			if (C_PARODD(tty))
				parity = MX_PARITY_MARK;
			else
				parity = MX_PARITY_SPACE;
		} else {
			if (C_PARODD(tty))
				parity = MX_PARITY_ODD;
			else
				parity = MX_PARITY_EVEN;
		}
	} else {
		parity = MX_PARITY_NONE;
	}

	/* Set stop bit of termios */
	if (C_CSTOPB(tty))
		stop_bits = MX_STOP_BITS_2;
	else
		stop_bits = MX_STOP_BITS_1;

	buf[0] = data_bits;
	buf[1] = parity;
	buf[2] = stop_bits;
	buf[3] = 0;

	err = mxuport_send_ctrl_data_urb(serial, RQ_VENDOR_SET_LINE,
					 0, port->port_number, buf, 4);
	if (err)
		goto out;

	err = mxuport_set_termios_flow(tty, old_termios, port, serial);
	if (err)
		goto out;

	baud = tty_get_baud_rate(tty);
	if (!baud)
		baud = 9600;

	/* Note: Little Endian */
	put_unaligned_le32(baud, buf);

	err = mxuport_send_ctrl_data_urb(serial, RQ_VENDOR_SET_BAUD,
					 0, port->port_number,
					 buf, 4);
	if (err)
		goto out;

	dev_dbg(&port->dev, "baud_rate	: %d\n", baud);
	dev_dbg(&port->dev, "data_bits	: %d\n", data_bits);
	dev_dbg(&port->dev, "parity	: %d\n", parity);
	dev_dbg(&port->dev, "stop_bits	: %d\n", stop_bits);

out:
	kfree(buf);
}

/*
 * Determine how many ports this device has dynamically.  It will be
 * called after the probe() callback is called, but before attach().
 */
static int mxuport_calc_num_ports(struct usb_serial *serial,
					struct usb_serial_endpoints *epds)
{
	unsigned long features = (unsigned long)usb_get_serial_data(serial);
	int num_ports;
	int i;

	if (features & MX_PORTS_MASK) {
		num_ports = (features & MX_PORTS_MASK) + MX_PORTS_OFFSET;
	} else {
		dev_warn(&serial->interface->dev,
				"unknown device, assuming two ports\n");
		num_ports = 2;
	}

	/*
	 * Setup bulk-out endpoint multiplexing. All ports share the same
	 * bulk-out endpoint.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(epds->bulk_out) < 16);

	/*
	 * The bulk-out buffers must be large enough for the four-byte header
	 * (and following data), but assume anything smaller than eight bytes
	 * is broken.
	 */
	if (usb_endpoint_maxp(epds->bulk_out[0]) < 8)
		return -EINVAL;

	for (i = 1; i < num_ports; ++i)
		epds->bulk_out[i] = epds->bulk_out[0];

	epds->num_bulk_out = num_ports;

	return num_ports;
}

static void mxuport_compose_fw_version(struct mxuport_fw_version *version,
				       u8 major, u8 minor, u8 build)
{
	version->major = major;
	version->minor = minor;
	version->build = build;
	version->value = (major << 16) | (minor << 8) | build;
}

static int mxuport_parse_fw_version(const struct firmware *fw,
				    const u16 *offsets,
				    struct mxuport_fw_version *version)
{
	if (fw->size <= offsets[2])
		return -EINVAL;

	mxuport_compose_fw_version(version, fw->data[offsets[0]],
				   fw->data[offsets[1]], fw->data[offsets[2]]);

	return 0;
}

static bool mxuport_is_mux50u_device(unsigned long features)
{
	switch (MX_FAMILY(features)) {
	case MX_UPORT_G2:
	case MX_PLATFORM_UART:
		return true;
	default:
		return false;
	}
}

static void mxuport_get_fw_name(unsigned long features, u16 productid,
				char *buf, size_t size)
{
	switch (MX_FAMILY(features)) {
	case MX_UPORT_G2:
		strscpy(buf, "moxa/moxa-up-mux50u.fw", size);
		break;
	case MX_PLATFORM_UART:
		strscpy(buf, "moxa/moxa-pf-mux50u.fw", size);
		break;
	default:
		snprintf(buf, size, "moxa/moxa-%04x.fw", productid);
		break;
	}
}

static int mxuport_fw_running_from_sram(struct usb_serial *serial, bool *from_sram)
{
	int err;
	u32 reg;
	u8 *buf;

	buf = kzalloc_objs(*buf, sizeof(reg));
	if (!buf)
		return -ENOMEM;

	err = mxuport_recv_ctrl_urb(serial, RQ_VENDOR_GET_SYS_REG,
				    MX_SYS_REG_REMAP_OFF, 0, buf, sizeof(reg));
	if (err < 0)
		goto out;

	reg = get_unaligned_be32(buf);
	*from_sram = (((reg >> 24) & MX_PWR_REMAP_MASK) == MX_REMAP_TO_SRAM);
	err = 0;
out:
	kfree(buf);
	return err;
}

/* Get the version of the firmware currently running. */
static int mxuport_get_fw_version(struct usb_serial *serial,
				  struct mxuport_fw_version *version)
{
	u8 *ver_buf;
	int err;

	ver_buf = kzalloc(4, GFP_KERNEL);
	if (!ver_buf)
		return -ENOMEM;

	/* Get firmware version from SDRAM */
	err = mxuport_recv_ctrl_urb(serial, RQ_VENDOR_GET_VERSION, 0, 0,
				    ver_buf, 4);
	if (err != 4) {
		err = -EIO;
		goto out;
	}

	mxuport_compose_fw_version(version, ver_buf[0], ver_buf[1], ver_buf[2]);
	err = 0;
out:
	kfree(ver_buf);
	return err;
}

/* Given a firmware blob, download it to the device. */
static int mxuport_download_fw(struct usb_serial *serial,
			       const struct firmware *fw_p)
{
	u8 *fw_buf;
	size_t txlen;
	size_t fwidx;
	int err;

	fw_buf = kmalloc(DOWN_BLOCK_SIZE, GFP_KERNEL);
	if (!fw_buf)
		return -ENOMEM;

	dev_dbg(&serial->interface->dev, "Starting firmware download...\n");
	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_START_FW_DOWN, 0, 0);
	if (err)
		goto out;

	fwidx = 0;
	do {
		txlen = min_t(size_t, (fw_p->size - fwidx), DOWN_BLOCK_SIZE);

		memcpy(fw_buf, &fw_p->data[fwidx], txlen);
		err = mxuport_send_ctrl_data_urb(serial, RQ_VENDOR_FW_DATA,
						 0, 0, fw_buf, txlen);
		if (err) {
			mxuport_send_ctrl_urb(serial, RQ_VENDOR_STOP_FW_DOWN,
					      0, 0);
			goto out;
		}

		fwidx += txlen;
		usleep_range(1000, 2000);

	} while (fwidx < fw_p->size);

	msleep(1000);
	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_STOP_FW_DOWN, 0, 0);
	if (err)
		goto out;

	msleep(1000);
	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_QUERY_FW_READY, 0, 0);

out:
	kfree(fw_buf);
	return err;
}

static int mxuport_probe(struct usb_serial *serial,
			 const struct usb_device_id *id)
{
	u16 productid = le16_to_cpu(serial->dev->descriptor.idProduct);
	struct mxuport_fw_version version, local_ver;
	unsigned long features = id->driver_info;
	const struct firmware *fw_p = NULL;
	const u16 *fw_version_offsets;
	bool download_fw, from_sram;
	char buf[32];
	int err;

	/* Load our firmware */
	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_QUERY_FW_CONFIG, 0, 0);
	if (err) {
		mxuport_send_ctrl_urb(serial, RQ_VENDOR_RESET_DEVICE, 0, 0);
		return err;
	}

	err = mxuport_get_fw_version(serial, &version);
	if (err < 0)
		return err;

	dev_dbg(&serial->interface->dev, "Device firmware version v%u.%u.%u\n",
		version.major, version.minor, version.build);

	if (mxuport_is_mux50u_device(features))
		fw_version_offsets = mxuport_mux50u_fw_ver_offsets;
	else
		fw_version_offsets = mxuport_fw_ver_offsets;

	mxuport_get_fw_name(features, productid, buf, sizeof(buf));

	err = request_firmware(&fw_p, buf, &serial->interface->dev);
	if (err) {
		dev_warn(&serial->interface->dev, "Firmware %s not found\n",
			 buf);

		/* Use the firmware already in the device */
		err = 0;
	} else {
		err = mxuport_parse_fw_version(fw_p, fw_version_offsets, &local_ver);
		if (err) {
			dev_err(&serial->interface->dev,
				"Firmware %s is too short\n", buf);
			goto out;
		}

		dev_dbg(&serial->interface->dev,
			"Available firmware version v%u.%u.%u\n",
			local_ver.major, local_ver.minor, local_ver.build);

		if (mxuport_is_mux50u_device(features)) {
			err = mxuport_fw_running_from_sram(serial, &from_sram);
			if (err)
				goto out;

			download_fw = !from_sram;
		} else {
			download_fw = local_ver.value > version.value;
		}

		if (download_fw) {
			err = mxuport_download_fw(serial, fw_p);
			if (err)
				goto out;
			err = mxuport_get_fw_version(serial, &version);
			if (err < 0)
				goto out;
		}
	}

	dev_info(&serial->interface->dev,
		 "Using device firmware version v%u.%u.%u\n",
		 version.major, version.minor, version.build);

	/*
	 * Contains the features of this hardware. Store away for
	 * later use, eg, number of ports.
	 */
	usb_set_serial_data(serial, (void *)id->driver_info);
out:
	if (fw_p)
		release_firmware(fw_p);
	return err;
}


static int mxuport_port_probe(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct mxuport_port *mxport;
	int err;

	mxport = devm_kzalloc(&port->dev, sizeof(struct mxuport_port),
			      GFP_KERNEL);
	if (!mxport)
		return -ENOMEM;

	mutex_init(&mxport->mutex);
	spin_lock_init(&mxport->spinlock);

	/* Set the port private data */
	usb_set_serial_port_data(port, mxport);

	/* Set FIFO (Enable) */
	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_FIFO_DISABLE,
				    0, port->port_number);
	if (err)
		return err;

	/* Set transmission mode (Hi-Performance) */
	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_HIGH_PERFOR,
				    0, port->port_number);
	if (err)
		return err;

	/* Set interface (RS-232) */
	return mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_INTERFACE,
				     MX_INT_RS232,
				     port->port_number);
}

static int mxuport_attach(struct usb_serial *serial)
{
	struct usb_serial_port *port0 = serial->port[0];
	struct usb_serial_port *port1 = serial->port[1];
	int err;

	/*
	 * All data from the ports is received on the first bulk in
	 * endpoint, with a multiplex header. The second bulk in is
	 * used for events.
	 *
	 * Start to read from the device.
	 */
	err = usb_serial_generic_submit_read_urbs(port0, GFP_KERNEL);
	if (err)
		return err;

	err = usb_serial_generic_submit_read_urbs(port1, GFP_KERNEL);
	if (err) {
		usb_serial_generic_close(port0);
		return err;
	}

	return 0;
}

static void mxuport_release(struct usb_serial *serial)
{
	struct usb_serial_port *port0 = serial->port[0];
	struct usb_serial_port *port1 = serial->port[1];

	usb_serial_generic_close(port1);
	usb_serial_generic_close(port0);
}

static int mxuport_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct mxuport_port *mxport = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	unsigned long flags;
	int err;

	/* Set receive host (enable) */
	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_RX_HOST_EN,
				    1, port->port_number);
	if (err)
		return err;

	err = mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_OPEN,
				    1, port->port_number);
	if (err) {
		mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_RX_HOST_EN,
				      0, port->port_number);
		return err;
	}

	/* Initial port termios */
	if (tty)
		mxuport_set_termios(tty, port, NULL);

	spin_lock_irqsave(&port->lock, flags);
	mxport->sent_payload = 0;
	mxport->hold_reason &= ~MX_WAIT_FOR_SEND_NEXT;
	spin_unlock_irqrestore(&port->lock, flags);

	/*
	 * TODO: use RQ_VENDOR_GET_MSR, once we know what it
	 * returns.
	 */
	mxport->msr_state = 0;

	return err;
}

static void mxuport_close(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;

	mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_OPEN, 0,
			      port->port_number);

	mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_RX_HOST_EN, 0,
			      port->port_number);
}

/* Send a break to the port. */
static int mxuport_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	int enable;

	if (break_state == -1) {
		enable = 1;
		dev_dbg(&port->dev, "%s - sending break\n", __func__);
	} else {
		enable = 0;
		dev_dbg(&port->dev, "%s - clearing break\n", __func__);
	}

	return mxuport_send_ctrl_urb(serial, RQ_VENDOR_SET_BREAK,
				     enable, port->port_number);
}

static int mxuport_resume(struct usb_serial *serial)
{
	struct usb_serial_port *port;
	int c = 0;
	int i;
	int r;

	for (i = 0; i < 2; i++) {
		port = serial->port[i];

		r = usb_serial_generic_submit_read_urbs(port, GFP_NOIO);
		if (r < 0)
			c++;
	}

	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		if (!tty_port_initialized(&port->port))
			continue;

		r = usb_serial_generic_write_start(port, GFP_NOIO);
		if (r < 0)
			c++;
	}

	return c ? -EIO : 0;
}

static struct usb_serial_driver mxuport_device = {
	.driver = {
		.name =		"mxuport",
	},
	.description		= "MOXA UPort",
	.id_table		= mxuport_idtable,
	.num_bulk_in		= 2,
	.num_bulk_out		= 1,
	.probe			= mxuport_probe,
	.port_probe		= mxuport_port_probe,
	.attach			= mxuport_attach,
	.release		= mxuport_release,
	.calc_num_ports		= mxuport_calc_num_ports,
	.open			= mxuport_open,
	.close			= mxuport_close,
	.set_termios		= mxuport_set_termios,
	.break_ctl		= mxuport_break_ctl,
	.tx_empty		= mxuport_tx_empty,
	.tiocmiwait		= usb_serial_generic_tiocmiwait,
	.get_icount		= usb_serial_generic_get_icount,
	.throttle		= mxuport_throttle,
	.unthrottle		= mxuport_unthrottle,
	.tiocmget		= mxuport_tiocmget,
	.tiocmset		= mxuport_tiocmset,
	.dtr_rts		= mxuport_dtr_rts,
	.process_read_urb	= mxuport_process_read_urb,
	.write_ready		= mxuport_write_ready,
	.prepare_write_buffer	= mxuport_prepare_write_buffer,
	.write_rollback		= mxuport_write_rollback,
	.resume			= mxuport_resume,
};

static struct usb_serial_driver *const serial_drivers[] = {
	&mxuport_device, NULL
};

module_usb_serial_driver(serial_drivers, mxuport_idtable);

MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_AUTHOR("<support@moxa.com>");
MODULE_DESCRIPTION("Moxa UPORT USB Serial driver");
MODULE_LICENSE("GPL");
