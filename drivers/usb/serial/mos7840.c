// SPDX-License-Identifier: GPL-2.0+
/*
 * Clean ups from Moschip version and a few ioctl implementations by:
 *	Paul B Schroeder <pschroeder "at" uplogix "dot" com>
 *
 * Originally based on drivers/usb/serial/io_edgeport.c which is:
 *      Copyright (C) 2000 Inside Out Networks, All rights reserved.
 *      Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>

#define DRIVER_DESC "Moschip 7840/7820 USB Serial Driver"

/*
 * 16C50 UART register defines
 */

#define LCR_BITS_5             0x00	/* 5 bits/char */
#define LCR_BITS_6             0x01	/* 6 bits/char */
#define LCR_BITS_7             0x02	/* 7 bits/char */
#define LCR_BITS_8             0x03	/* 8 bits/char */
#define LCR_BITS_MASK          0x03	/* Mask for bits/char field */

#define LCR_STOP_1             0x00	/* 1 stop bit */
#define LCR_STOP_1_5           0x04	/* 1.5 stop bits (if 5   bits/char) */
#define LCR_STOP_2             0x04	/* 2 stop bits   (if 6-8 bits/char) */
#define LCR_STOP_MASK          0x04	/* Mask for stop bits field */

#define LCR_PAR_NONE           0x00	/* No parity */
#define LCR_PAR_ODD            0x08	/* Odd parity */
#define LCR_PAR_EVEN           0x18	/* Even parity */
#define LCR_PAR_MARK           0x28	/* Force parity bit to 1 */
#define LCR_PAR_SPACE          0x38	/* Force parity bit to 0 */
#define LCR_PAR_MASK           0x38	/* Mask for parity field */

#define LCR_SET_BREAK          0x40	/* Set Break condition */
#define LCR_DL_ENABLE          0x80	/* Enable access to divisor latch */

#define MCR_DTR                0x01	/* Assert DTR */
#define MCR_RTS                0x02	/* Assert RTS */
#define MCR_OUT1               0x04	/* Loopback only: Sets state of RI */
#define MCR_MASTER_IE          0x08	/* Enable interrupt outputs */
#define MCR_LOOPBACK           0x10	/* Set internal (digital) loopback mode */
#define MCR_XON_ANY            0x20	/* Enable any char to exit XOFF mode */

#define MOS7840_MSR_CTS        0x10	/* Current state of CTS */
#define MOS7840_MSR_DSR        0x20	/* Current state of DSR */
#define MOS7840_MSR_RI         0x40	/* Current state of RI */
#define MOS7840_MSR_CD         0x80	/* Current state of CD */

/*
 * Defines used for sending commands to port
 */

#define MOS_WDR_TIMEOUT		5000	/* default urb timeout */

#define MOS_PORT1       0x0200
#define MOS_PORT2       0x0300
#define MOS_VENREG      0x0000
#define MOS_MAX_PORT	0x02
#define MOS_WRITE       0x0E
#define MOS_READ        0x0D

/* Requests */
#define MCS_RD_RTYPE    0xC0
#define MCS_WR_RTYPE    0x40
#define MCS_RDREQ       0x0D
#define MCS_WRREQ       0x0E
#define MCS_CTRL_TIMEOUT        500
#define VENDOR_READ_LENGTH      (0x01)

#define MAX_NAME_LEN    64

#define ZLP_REG1  0x3A		/* Zero_Flag_Reg1    58 */
#define ZLP_REG5  0x3E		/* Zero_Flag_Reg5    62 */

/* For higher baud Rates use TIOCEXBAUD */
#define TIOCEXBAUD     0x5462

/*
 * Vendor id and device id defines
 *
 * NOTE: Do not add new defines, add entries directly to the id_table instead.
 */
#define USB_VENDOR_ID_BANDB              0x0856
#define BANDB_DEVICE_ID_USO9ML2_2        0xAC22
#define BANDB_DEVICE_ID_USO9ML2_2P       0xBC00
#define BANDB_DEVICE_ID_USO9ML2_4        0xAC24
#define BANDB_DEVICE_ID_USO9ML2_4P       0xBC01
#define BANDB_DEVICE_ID_US9ML2_2         0xAC29
#define BANDB_DEVICE_ID_US9ML2_4         0xAC30
#define BANDB_DEVICE_ID_USPTL4_2         0xAC31
#define BANDB_DEVICE_ID_USPTL4_4         0xAC32
#define BANDB_DEVICE_ID_USOPTL4_2        0xAC42
#define BANDB_DEVICE_ID_USOPTL4_2P       0xBC02
#define BANDB_DEVICE_ID_USOPTL4_4        0xAC44
#define BANDB_DEVICE_ID_USOPTL4_4P       0xBC03

/* Interrupt Routine Defines    */

#define SERIAL_IIR_RLS      0x06
#define SERIAL_IIR_MS       0x00

/*
 *  Emulation of the bit mask on the LINE STATUS REGISTER.
 */
#define SERIAL_LSR_DR       0x0001
#define SERIAL_LSR_OE       0x0002
#define SERIAL_LSR_PE       0x0004
#define SERIAL_LSR_FE       0x0008
#define SERIAL_LSR_BI       0x0010

#define MOS_MSR_DELTA_CTS   0x10
#define MOS_MSR_DELTA_DSR   0x20
#define MOS_MSR_DELTA_RI    0x40
#define MOS_MSR_DELTA_CD    0x80

/* Serial Port register Address */
#define INTERRUPT_ENABLE_REGISTER  ((__u16)(0x01))
#define FIFO_CONTROL_REGISTER      ((__u16)(0x02))
#define LINE_CONTROL_REGISTER      ((__u16)(0x03))
#define MODEM_CONTROL_REGISTER     ((__u16)(0x04))
#define LINE_STATUS_REGISTER       ((__u16)(0x05))
#define MODEM_STATUS_REGISTER      ((__u16)(0x06))
#define SCRATCH_PAD_REGISTER       ((__u16)(0x07))
#define DIVISOR_LATCH_LSB          ((__u16)(0x00))
#define DIVISOR_LATCH_MSB          ((__u16)(0x01))

#define CLK_MULTI_REGISTER         ((__u16)(0x02))
#define CLK_START_VALUE_REGISTER   ((__u16)(0x03))
#define GPIO_REGISTER              ((__u16)(0x07))

#define SERIAL_LCR_DLAB            ((__u16)(0x0080))

/*
 * URB POOL related defines
 */
#define NUM_URBS                        16	/* URB Count */
#define URB_TRANSFER_BUFFER_SIZE        32	/* URB Size  */

/* LED on/off milliseconds*/
#define LED_ON_MS	500
#define LED_OFF_MS	500

enum mos7840_flag {
	MOS7840_FLAG_LED_BUSY,
};

#define MCS_PORT_MASK	GENMASK(2, 0)
#define MCS_PORTS(nr)	((nr) & MCS_PORT_MASK)
#define MCS_LED		BIT(3)

#define MCS_DEVICE(vid, pid, flags) \
		USB_DEVICE((vid), (pid)), .driver_info = (flags)

static const struct usb_device_id id_table[] = {
	{ MCS_DEVICE(0x0557, 0x2011, MCS_PORTS(4)) },	/* ATEN UC2324 */
	{ MCS_DEVICE(0x0557, 0x7820, MCS_PORTS(2)) },	/* ATEN UC2322 */
	{ MCS_DEVICE(0x110a, 0x2210, MCS_PORTS(2)) },	/* Moxa UPort 2210 */
	{ MCS_DEVICE(0x9710, 0x7810, MCS_PORTS(1) | MCS_LED) }, /* ASIX MCS7810 */
	{ MCS_DEVICE(0x9710, 0x7820, MCS_PORTS(2)) },	/* MosChip MCS7820 */
	{ MCS_DEVICE(0x9710, 0x7840, MCS_PORTS(4)) },	/* MosChip MCS7840 */
	{ MCS_DEVICE(0x9710, 0x7843, MCS_PORTS(3)) },	/* ASIX MCS7840 3 port */
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USO9ML2_2) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USO9ML2_2P) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USO9ML2_4) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USO9ML2_4P) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_US9ML2_2) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_US9ML2_4) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USPTL4_2) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USPTL4_4) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USOPTL4_2) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USOPTL4_2P) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USOPTL4_4) },
	{ USB_DEVICE(USB_VENDOR_ID_BANDB, BANDB_DEVICE_ID_USOPTL4_4P) },
	{}			/* terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

/* This structure holds all of the local port information */

struct moschip_port {
	int port_num;		/*Actual port number in the device(1,2,etc) */
	struct urb *read_urb;	/* read URB for this port */
	__u8 shadowLCR;		/* last LCR value received */
	__u8 shadowMCR;		/* last MCR value received */
	struct usb_serial_port *port;	/* loop back to the owner of this object */

	/* Offsets */
	__u8 SpRegOffset;
	__u8 ControlRegOffset;
	__u8 DcrRegOffset;

	spinlock_t pool_lock;
	struct urb *write_urb_pool[NUM_URBS];
	char busy[NUM_URBS];
	bool read_urb_busy;

	/* For device(s) with LED indicator */
	bool has_led;
	struct timer_list led_timer1;	/* Timer for LED on */
	struct timer_list led_timer2;	/* Timer for LED off */
	struct urb *led_urb;
	struct usb_ctrlrequest *led_dr;

	unsigned long flags;
};

/*
 * mos7840_set_reg_sync
 * 	To set the Control register by calling usb_fill_control_urb function
 *	by passing usb_sndctrlpipe function as parameter.
 */

static int mos7840_set_reg_sync(struct usb_serial_port *port, __u16 reg,
				__u16 val)
{
	struct usb_device *dev = port->serial->dev;
	val = val & 0x00ff;
	dev_dbg(&port->dev, "mos7840_set_reg_sync offset is %x, value %x\n", reg, val);

	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), MCS_WRREQ,
			       MCS_WR_RTYPE, val, reg, NULL, 0,
			       MOS_WDR_TIMEOUT);
}

/*
 * mos7840_get_reg_sync
 * 	To set the Uart register by calling usb_fill_control_urb function by
 *	passing usb_rcvctrlpipe function as parameter.
 */

static int mos7840_get_reg_sync(struct usb_serial_port *port, __u16 reg,
				__u16 *val)
{
	struct usb_device *dev = port->serial->dev;
	int ret = 0;
	u8 *buf;

	buf = kmalloc(VENDOR_READ_LENGTH, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), MCS_RDREQ,
			      MCS_RD_RTYPE, 0, reg, buf, VENDOR_READ_LENGTH,
			      MOS_WDR_TIMEOUT);
	if (ret < VENDOR_READ_LENGTH) {
		if (ret >= 0)
			ret = -EIO;
		goto out;
	}

	*val = buf[0];
	dev_dbg(&port->dev, "%s offset is %x, return val %x\n", __func__, reg, *val);
out:
	kfree(buf);
	return ret;
}

/*
 * mos7840_set_uart_reg
 *	To set the Uart register by calling usb_fill_control_urb function by
 *	passing usb_sndctrlpipe function as parameter.
 */

static int mos7840_set_uart_reg(struct usb_serial_port *port, __u16 reg,
				__u16 val)
{

	struct usb_device *dev = port->serial->dev;
	val = val & 0x00ff;
	/* For the UART control registers, the application number need
	   to be Or'ed */
	if (port->serial->num_ports == 2 && port->port_number != 0)
		val |= ((__u16)port->port_number + 2) << 8;
	else
		val |= ((__u16)port->port_number + 1) << 8;
	dev_dbg(&port->dev, "%s application number is %x\n", __func__, val);
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), MCS_WRREQ,
			       MCS_WR_RTYPE, val, reg, NULL, 0,
			       MOS_WDR_TIMEOUT);

}

/*
 * mos7840_get_uart_reg
 *	To set the Control register by calling usb_fill_control_urb function
 *	by passing usb_rcvctrlpipe function as parameter.
 */
static int mos7840_get_uart_reg(struct usb_serial_port *port, __u16 reg,
				__u16 *val)
{
	struct usb_device *dev = port->serial->dev;
	int ret = 0;
	__u16 Wval;
	u8 *buf;

	buf = kmalloc(VENDOR_READ_LENGTH, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Wval  is same as application number */
	if (port->serial->num_ports == 2 && port->port_number != 0)
		Wval = ((__u16)port->port_number + 2) << 8;
	else
		Wval = ((__u16)port->port_number + 1) << 8;
	dev_dbg(&port->dev, "%s application number is %x\n", __func__, Wval);
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), MCS_RDREQ,
			      MCS_RD_RTYPE, Wval, reg, buf, VENDOR_READ_LENGTH,
			      MOS_WDR_TIMEOUT);
	if (ret < VENDOR_READ_LENGTH) {
		if (ret >= 0)
			ret = -EIO;
		goto out;
	}
	*val = buf[0];
out:
	kfree(buf);
	return ret;
}

static void mos7840_dump_serial_port(struct usb_serial_port *port,
				     struct moschip_port *mos7840_port)
{

	dev_dbg(&port->dev, "SpRegOffset is %2x\n", mos7840_port->SpRegOffset);
	dev_dbg(&port->dev, "ControlRegOffset is %2x\n", mos7840_port->ControlRegOffset);
	dev_dbg(&port->dev, "DCRRegOffset is %2x\n", mos7840_port->DcrRegOffset);

}

/************************************************************************/
/************************************************************************/
/*            U S B  C A L L B A C K   F U N C T I O N S                */
/*            U S B  C A L L B A C K   F U N C T I O N S                */
/************************************************************************/
/************************************************************************/

static void mos7840_set_led_callback(struct urb *urb)
{
	switch (urb->status) {
	case 0:
		/* Success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* This urb is terminated, clean up */
		dev_dbg(&urb->dev->dev, "%s - urb shutting down: %d\n",
			__func__, urb->status);
		break;
	default:
		dev_dbg(&urb->dev->dev, "%s - nonzero urb status: %d\n",
			__func__, urb->status);
	}
}

static void mos7840_set_led_async(struct moschip_port *mcs, __u16 wval,
				__u16 reg)
{
	struct usb_device *dev = mcs->port->serial->dev;
	struct usb_ctrlrequest *dr = mcs->led_dr;

	dr->bRequestType = MCS_WR_RTYPE;
	dr->bRequest = MCS_WRREQ;
	dr->wValue = cpu_to_le16(wval);
	dr->wIndex = cpu_to_le16(reg);
	dr->wLength = cpu_to_le16(0);

	usb_fill_control_urb(mcs->led_urb, dev, usb_sndctrlpipe(dev, 0),
		(unsigned char *)dr, NULL, 0, mos7840_set_led_callback, NULL);

	usb_submit_urb(mcs->led_urb, GFP_ATOMIC);
}

static void mos7840_set_led_sync(struct usb_serial_port *port, __u16 reg,
				__u16 val)
{
	struct usb_device *dev = port->serial->dev;

	usb_control_msg(dev, usb_sndctrlpipe(dev, 0), MCS_WRREQ, MCS_WR_RTYPE,
			val, reg, NULL, 0, MOS_WDR_TIMEOUT);
}

static void mos7840_led_off(struct timer_list *t)
{
	struct moschip_port *mcs = from_timer(mcs, t, led_timer1);

	/* Turn off LED */
	mos7840_set_led_async(mcs, 0x0300, MODEM_CONTROL_REGISTER);
	mod_timer(&mcs->led_timer2,
				jiffies + msecs_to_jiffies(LED_OFF_MS));
}

static void mos7840_led_flag_off(struct timer_list *t)
{
	struct moschip_port *mcs = from_timer(mcs, t, led_timer2);

	clear_bit_unlock(MOS7840_FLAG_LED_BUSY, &mcs->flags);
}

static void mos7840_led_activity(struct usb_serial_port *port)
{
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);

	if (test_and_set_bit_lock(MOS7840_FLAG_LED_BUSY, &mos7840_port->flags))
		return;

	mos7840_set_led_async(mos7840_port, 0x0301, MODEM_CONTROL_REGISTER);
	mod_timer(&mos7840_port->led_timer1,
				jiffies + msecs_to_jiffies(LED_ON_MS));
}

/*****************************************************************************
 * mos7840_bulk_in_callback
 *	this is the callback function for when we have received data on the
 *	bulk in endpoint.
 *****************************************************************************/

static void mos7840_bulk_in_callback(struct urb *urb)
{
	struct moschip_port *mos7840_port = urb->context;
	struct usb_serial_port *port = mos7840_port->port;
	int retval;
	unsigned char *data;
	int status = urb->status;

	if (status) {
		dev_dbg(&urb->dev->dev, "nonzero read bulk status received: %d\n", status);
		mos7840_port->read_urb_busy = false;
		return;
	}

	data = urb->transfer_buffer;
	usb_serial_debug_data(&port->dev, __func__, urb->actual_length, data);

	if (urb->actual_length) {
		struct tty_port *tport = &mos7840_port->port->port;
		tty_insert_flip_string(tport, data, urb->actual_length);
		tty_flip_buffer_push(tport);
		port->icount.rx += urb->actual_length;
		dev_dbg(&port->dev, "icount.rx is %d:\n", port->icount.rx);
	}

	if (mos7840_port->has_led)
		mos7840_led_activity(port);

	mos7840_port->read_urb_busy = true;
	retval = usb_submit_urb(mos7840_port->read_urb, GFP_ATOMIC);

	if (retval) {
		dev_dbg(&port->dev, "usb_submit_urb(read bulk) failed, retval = %d\n", retval);
		mos7840_port->read_urb_busy = false;
	}
}

/*****************************************************************************
 * mos7840_bulk_out_data_callback
 *	this is the callback function for when we have finished sending
 *	serial data on the bulk out endpoint.
 *****************************************************************************/

static void mos7840_bulk_out_data_callback(struct urb *urb)
{
	struct moschip_port *mos7840_port = urb->context;
	struct usb_serial_port *port = mos7840_port->port;
	int status = urb->status;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&mos7840_port->pool_lock, flags);
	for (i = 0; i < NUM_URBS; i++) {
		if (urb == mos7840_port->write_urb_pool[i]) {
			mos7840_port->busy[i] = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&mos7840_port->pool_lock, flags);

	if (status) {
		dev_dbg(&port->dev, "nonzero write bulk status received:%d\n", status);
		return;
	}

	tty_port_tty_wakeup(&port->port);

}

/************************************************************************/
/*       D R I V E R  T T Y  I N T E R F A C E  F U N C T I O N S       */
/************************************************************************/

/*****************************************************************************
 * mos7840_open
 *	this function is called by the tty driver when a port is opened
 *	If successful, we return 0
 *	Otherwise we return a negative error number.
 *****************************************************************************/

static int mos7840_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	int response;
	int j;
	struct urb *urb;
	__u16 Data;
	int status;

	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

	/* Initialising the write urb pool */
	for (j = 0; j < NUM_URBS; ++j) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		mos7840_port->write_urb_pool[j] = urb;
		if (!urb)
			continue;

		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE,
								GFP_KERNEL);
		if (!urb->transfer_buffer) {
			usb_free_urb(urb);
			mos7840_port->write_urb_pool[j] = NULL;
			continue;
		}
	}

/*****************************************************************************
 * Initialize MCS7840 -- Write Init values to corresponding Registers
 *
 * Register Index
 * 1 : IER
 * 2 : FCR
 * 3 : LCR
 * 4 : MCR
 *
 * 0x08 : SP1/2 Control Reg
 *****************************************************************************/

	/* NEED to check the following Block */

	Data = 0x0;
	status = mos7840_get_reg_sync(port, mos7840_port->SpRegOffset, &Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Reading Spreg failed\n");
		goto err;
	}
	Data |= 0x80;
	status = mos7840_set_reg_sync(port, mos7840_port->SpRegOffset, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "writing Spreg failed\n");
		goto err;
	}

	Data &= ~0x80;
	status = mos7840_set_reg_sync(port, mos7840_port->SpRegOffset, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "writing Spreg failed\n");
		goto err;
	}
	/* End of block to be checked */

	Data = 0x0;
	status = mos7840_get_reg_sync(port, mos7840_port->ControlRegOffset,
									&Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Reading Controlreg failed\n");
		goto err;
	}
	Data |= 0x08;		/* Driver done bit */
	Data |= 0x20;		/* rx_disable */
	status = mos7840_set_reg_sync(port,
				mos7840_port->ControlRegOffset, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "writing Controlreg failed\n");
		goto err;
	}
	/* do register settings here */
	/* Set all regs to the device default values. */
	/***********************************
	 * First Disable all interrupts.
	 ***********************************/
	Data = 0x00;
	status = mos7840_set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "disabling interrupts failed\n");
		goto err;
	}
	/* Set FIFO_CONTROL_REGISTER to the default value */
	Data = 0x00;
	status = mos7840_set_uart_reg(port, FIFO_CONTROL_REGISTER, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Writing FIFO_CONTROL_REGISTER  failed\n");
		goto err;
	}

	Data = 0xcf;
	status = mos7840_set_uart_reg(port, FIFO_CONTROL_REGISTER, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Writing FIFO_CONTROL_REGISTER  failed\n");
		goto err;
	}

	Data = 0x03;
	status = mos7840_set_uart_reg(port, LINE_CONTROL_REGISTER, Data);
	mos7840_port->shadowLCR = Data;

	Data = 0x0b;
	status = mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);
	mos7840_port->shadowMCR = Data;

	Data = 0x00;
	status = mos7840_get_uart_reg(port, LINE_CONTROL_REGISTER, &Data);
	mos7840_port->shadowLCR = Data;

	Data |= SERIAL_LCR_DLAB;	/* data latch enable in LCR 0x80 */
	status = mos7840_set_uart_reg(port, LINE_CONTROL_REGISTER, Data);

	Data = 0x0c;
	status = mos7840_set_uart_reg(port, DIVISOR_LATCH_LSB, Data);

	Data = 0x0;
	status = mos7840_set_uart_reg(port, DIVISOR_LATCH_MSB, Data);

	Data = 0x00;
	status = mos7840_get_uart_reg(port, LINE_CONTROL_REGISTER, &Data);

	Data = Data & ~SERIAL_LCR_DLAB;
	status = mos7840_set_uart_reg(port, LINE_CONTROL_REGISTER, Data);
	mos7840_port->shadowLCR = Data;

	/* clearing Bulkin and Bulkout Fifo */
	Data = 0x0;
	status = mos7840_get_reg_sync(port, mos7840_port->SpRegOffset, &Data);

	Data = Data | 0x0c;
	status = mos7840_set_reg_sync(port, mos7840_port->SpRegOffset, Data);

	Data = Data & ~0x0c;
	status = mos7840_set_reg_sync(port, mos7840_port->SpRegOffset, Data);
	/* Finally enable all interrupts */
	Data = 0x0c;
	status = mos7840_set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	/* clearing rx_disable */
	Data = 0x0;
	status = mos7840_get_reg_sync(port, mos7840_port->ControlRegOffset,
									&Data);
	Data = Data & ~0x20;
	status = mos7840_set_reg_sync(port, mos7840_port->ControlRegOffset,
									Data);

	/* rx_negate */
	Data = 0x0;
	status = mos7840_get_reg_sync(port, mos7840_port->ControlRegOffset,
									&Data);
	Data = Data | 0x10;
	status = mos7840_set_reg_sync(port, mos7840_port->ControlRegOffset,
									Data);

	dev_dbg(&port->dev, "port number is %d\n", port->port_number);
	dev_dbg(&port->dev, "minor number is %d\n", port->minor);
	dev_dbg(&port->dev, "Bulkin endpoint is %d\n", port->bulk_in_endpointAddress);
	dev_dbg(&port->dev, "BulkOut endpoint is %d\n", port->bulk_out_endpointAddress);
	dev_dbg(&port->dev, "Interrupt endpoint is %d\n", port->interrupt_in_endpointAddress);
	dev_dbg(&port->dev, "port's number in the device is %d\n", mos7840_port->port_num);
	mos7840_port->read_urb = port->read_urb;

	/* set up our bulk in urb */
	if ((serial->num_ports == 2) && (((__u16)port->port_number % 2) != 0)) {
		usb_fill_bulk_urb(mos7840_port->read_urb,
			serial->dev,
			usb_rcvbulkpipe(serial->dev,
				(port->bulk_in_endpointAddress) + 2),
			port->bulk_in_buffer,
			mos7840_port->read_urb->transfer_buffer_length,
			mos7840_bulk_in_callback, mos7840_port);
	} else {
		usb_fill_bulk_urb(mos7840_port->read_urb,
			serial->dev,
			usb_rcvbulkpipe(serial->dev,
				port->bulk_in_endpointAddress),
			port->bulk_in_buffer,
			mos7840_port->read_urb->transfer_buffer_length,
			mos7840_bulk_in_callback, mos7840_port);
	}

	dev_dbg(&port->dev, "%s: bulkin endpoint is %d\n", __func__, port->bulk_in_endpointAddress);
	mos7840_port->read_urb_busy = true;
	response = usb_submit_urb(mos7840_port->read_urb, GFP_KERNEL);
	if (response) {
		dev_err(&port->dev, "%s - Error %d submitting control urb\n",
			__func__, response);
		mos7840_port->read_urb_busy = false;
	}

	/* initialize our port settings */
	/* Must set to enable ints! */
	mos7840_port->shadowMCR = MCR_MASTER_IE;

	return 0;
err:
	for (j = 0; j < NUM_URBS; ++j) {
		urb = mos7840_port->write_urb_pool[j];
		if (!urb)
			continue;
		kfree(urb->transfer_buffer);
		usb_free_urb(urb);
	}
	return status;
}

/*****************************************************************************
 * mos7840_chars_in_buffer
 *	this function is called by the tty driver when it wants to know how many
 *	bytes of data we currently have outstanding in the port (data that has
 *	been written, but hasn't made it out the port yet)
 *	If successful, we return the number of bytes left to be written in the
 *	system,
 *	Otherwise we return zero.
 *****************************************************************************/

static int mos7840_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	int i;
	int chars = 0;
	unsigned long flags;

	spin_lock_irqsave(&mos7840_port->pool_lock, flags);
	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7840_port->busy[i]) {
			struct urb *urb = mos7840_port->write_urb_pool[i];
			chars += urb->transfer_buffer_length;
		}
	}
	spin_unlock_irqrestore(&mos7840_port->pool_lock, flags);
	dev_dbg(&port->dev, "%s - returns %d\n", __func__, chars);
	return chars;

}

/*****************************************************************************
 * mos7840_close
 *	this function is called by the tty driver when a port is closed
 *****************************************************************************/

static void mos7840_close(struct usb_serial_port *port)
{
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	int j;
	__u16 Data;

	for (j = 0; j < NUM_URBS; ++j)
		usb_kill_urb(mos7840_port->write_urb_pool[j]);

	/* Freeing Write URBs */
	for (j = 0; j < NUM_URBS; ++j) {
		if (mos7840_port->write_urb_pool[j]) {
			kfree(mos7840_port->write_urb_pool[j]->transfer_buffer);
			usb_free_urb(mos7840_port->write_urb_pool[j]);
		}
	}

	usb_kill_urb(mos7840_port->read_urb);
	mos7840_port->read_urb_busy = false;

	Data = 0x0;
	mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);

	Data = 0x00;
	mos7840_set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);
}

/*****************************************************************************
 * mos7840_break
 *	this function sends a break to the port
 *****************************************************************************/
static void mos7840_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	unsigned char data;

	if (break_state == -1)
		data = mos7840_port->shadowLCR | LCR_SET_BREAK;
	else
		data = mos7840_port->shadowLCR & ~LCR_SET_BREAK;

	/* FIXME: no locking on shadowLCR anywhere in driver */
	mos7840_port->shadowLCR = data;
	dev_dbg(&port->dev, "%s mos7840_port->shadowLCR is %x\n", __func__, mos7840_port->shadowLCR);
	mos7840_set_uart_reg(port, LINE_CONTROL_REGISTER,
			     mos7840_port->shadowLCR);
}

/*****************************************************************************
 * mos7840_write_room
 *	this function is called by the tty driver when it wants to know how many
 *	bytes of data we can accept for a specific port.
 *	If successful, we return the amount of room that we have for this port
 *	Otherwise we return a negative error number.
 *****************************************************************************/

static int mos7840_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	int i;
	int room = 0;
	unsigned long flags;

	spin_lock_irqsave(&mos7840_port->pool_lock, flags);
	for (i = 0; i < NUM_URBS; ++i) {
		if (!mos7840_port->busy[i])
			room += URB_TRANSFER_BUFFER_SIZE;
	}
	spin_unlock_irqrestore(&mos7840_port->pool_lock, flags);

	room = (room == 0) ? 0 : room - URB_TRANSFER_BUFFER_SIZE + 1;
	dev_dbg(&mos7840_port->port->dev, "%s - returns %d\n", __func__, room);
	return room;

}

/*****************************************************************************
 * mos7840_write
 *	this function is called by the tty driver when data should be written to
 *	the port.
 *	If successful, we return the number of bytes written, otherwise we
 *      return a negative error number.
 *****************************************************************************/

static int mos7840_write(struct tty_struct *tty, struct usb_serial_port *port,
			 const unsigned char *data, int count)
{
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	int status;
	int i;
	int bytes_sent = 0;
	int transfer_size;
	unsigned long flags;
	struct urb *urb;
	/* __u16 Data; */
	const unsigned char *current_position = data;

	/* try to find a free urb in the list */
	urb = NULL;

	spin_lock_irqsave(&mos7840_port->pool_lock, flags);
	for (i = 0; i < NUM_URBS; ++i) {
		if (!mos7840_port->busy[i]) {
			mos7840_port->busy[i] = 1;
			urb = mos7840_port->write_urb_pool[i];
			dev_dbg(&port->dev, "URB:%d\n", i);
			break;
		}
	}
	spin_unlock_irqrestore(&mos7840_port->pool_lock, flags);

	if (urb == NULL) {
		dev_dbg(&port->dev, "%s - no more free urbs\n", __func__);
		goto exit;
	}

	if (urb->transfer_buffer == NULL) {
		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE,
					       GFP_ATOMIC);
		if (!urb->transfer_buffer) {
			bytes_sent = -ENOMEM;
			goto exit;
		}
	}
	transfer_size = min(count, URB_TRANSFER_BUFFER_SIZE);

	memcpy(urb->transfer_buffer, current_position, transfer_size);

	/* fill urb with data and submit  */
	if ((serial->num_ports == 2) && (((__u16)port->port_number % 2) != 0)) {
		usb_fill_bulk_urb(urb,
			serial->dev,
			usb_sndbulkpipe(serial->dev,
				(port->bulk_out_endpointAddress) + 2),
			urb->transfer_buffer,
			transfer_size,
			mos7840_bulk_out_data_callback, mos7840_port);
	} else {
		usb_fill_bulk_urb(urb,
			serial->dev,
			usb_sndbulkpipe(serial->dev,
				port->bulk_out_endpointAddress),
			urb->transfer_buffer,
			transfer_size,
			mos7840_bulk_out_data_callback, mos7840_port);
	}

	dev_dbg(&port->dev, "bulkout endpoint is %d\n", port->bulk_out_endpointAddress);

	if (mos7840_port->has_led)
		mos7840_led_activity(port);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_ATOMIC);

	if (status) {
		mos7840_port->busy[i] = 0;
		dev_err_console(port, "%s - usb_submit_urb(write bulk) failed "
			"with status = %d\n", __func__, status);
		bytes_sent = status;
		goto exit;
	}
	bytes_sent = transfer_size;
	port->icount.tx += transfer_size;
	dev_dbg(&port->dev, "icount.tx is %d:\n", port->icount.tx);
exit:
	return bytes_sent;

}

/*****************************************************************************
 * mos7840_throttle
 *	this function is called by the tty driver when it wants to stop the data
 *	being read from the port.
 *****************************************************************************/

static void mos7840_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	int status;

	/* if we are implementing XON/XOFF, send the stop character */
	if (I_IXOFF(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		status = mos7840_write(tty, port, &stop_char, 1);
		if (status <= 0)
			return;
	}
	/* if we are implementing RTS/CTS, toggle that line */
	if (C_CRTSCTS(tty)) {
		mos7840_port->shadowMCR &= ~MCR_RTS;
		status = mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER,
					 mos7840_port->shadowMCR);
		if (status < 0)
			return;
	}
}

/*****************************************************************************
 * mos7840_unthrottle
 *	this function is called by the tty driver when it wants to resume
 *	the data being read from the port (called after mos7840_throttle is
 *	called)
 *****************************************************************************/
static void mos7840_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	int status;

	/* if we are implementing XON/XOFF, send the start character */
	if (I_IXOFF(tty)) {
		unsigned char start_char = START_CHAR(tty);
		status = mos7840_write(tty, port, &start_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (C_CRTSCTS(tty)) {
		mos7840_port->shadowMCR |= MCR_RTS;
		status = mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER,
					 mos7840_port->shadowMCR);
		if (status < 0)
			return;
	}
}

static int mos7840_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int result;
	__u16 msr;
	__u16 mcr;
	int status;

	status = mos7840_get_uart_reg(port, MODEM_STATUS_REGISTER, &msr);
	if (status < 0)
		return -EIO;
	status = mos7840_get_uart_reg(port, MODEM_CONTROL_REGISTER, &mcr);
	if (status < 0)
		return -EIO;
	result = ((mcr & MCR_DTR) ? TIOCM_DTR : 0)
	    | ((mcr & MCR_RTS) ? TIOCM_RTS : 0)
	    | ((mcr & MCR_LOOPBACK) ? TIOCM_LOOP : 0)
	    | ((msr & MOS7840_MSR_CTS) ? TIOCM_CTS : 0)
	    | ((msr & MOS7840_MSR_CD) ? TIOCM_CAR : 0)
	    | ((msr & MOS7840_MSR_RI) ? TIOCM_RI : 0)
	    | ((msr & MOS7840_MSR_DSR) ? TIOCM_DSR : 0);

	dev_dbg(&port->dev, "%s - 0x%04X\n", __func__, result);

	return result;
}

static int mos7840_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	unsigned int mcr;
	int status;

	/* FIXME: What locks the port registers ? */
	mcr = mos7840_port->shadowMCR;
	if (clear & TIOCM_RTS)
		mcr &= ~MCR_RTS;
	if (clear & TIOCM_DTR)
		mcr &= ~MCR_DTR;
	if (clear & TIOCM_LOOP)
		mcr &= ~MCR_LOOPBACK;

	if (set & TIOCM_RTS)
		mcr |= MCR_RTS;
	if (set & TIOCM_DTR)
		mcr |= MCR_DTR;
	if (set & TIOCM_LOOP)
		mcr |= MCR_LOOPBACK;

	mos7840_port->shadowMCR = mcr;

	status = mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER, mcr);
	if (status < 0) {
		dev_dbg(&port->dev, "setting MODEM_CONTROL_REGISTER Failed\n");
		return status;
	}

	return 0;
}

/*****************************************************************************
 * mos7840_calc_baud_rate_divisor
 *	this function calculates the proper baud rate divisor for the specified
 *	baud rate.
 *****************************************************************************/
static int mos7840_calc_baud_rate_divisor(struct usb_serial_port *port,
					  int baudRate, int *divisor,
					  __u16 *clk_sel_val)
{
	dev_dbg(&port->dev, "%s - %d\n", __func__, baudRate);

	if (baudRate <= 115200) {
		*divisor = 115200 / baudRate;
		*clk_sel_val = 0x0;
	}
	if ((baudRate > 115200) && (baudRate <= 230400)) {
		*divisor = 230400 / baudRate;
		*clk_sel_val = 0x10;
	} else if ((baudRate > 230400) && (baudRate <= 403200)) {
		*divisor = 403200 / baudRate;
		*clk_sel_val = 0x20;
	} else if ((baudRate > 403200) && (baudRate <= 460800)) {
		*divisor = 460800 / baudRate;
		*clk_sel_val = 0x30;
	} else if ((baudRate > 460800) && (baudRate <= 806400)) {
		*divisor = 806400 / baudRate;
		*clk_sel_val = 0x40;
	} else if ((baudRate > 806400) && (baudRate <= 921600)) {
		*divisor = 921600 / baudRate;
		*clk_sel_val = 0x50;
	} else if ((baudRate > 921600) && (baudRate <= 1572864)) {
		*divisor = 1572864 / baudRate;
		*clk_sel_val = 0x60;
	} else if ((baudRate > 1572864) && (baudRate <= 3145728)) {
		*divisor = 3145728 / baudRate;
		*clk_sel_val = 0x70;
	}
	return 0;
}

/*****************************************************************************
 * mos7840_send_cmd_write_baud_rate
 *	this function sends the proper command to change the baud rate of the
 *	specified port.
 *****************************************************************************/

static int mos7840_send_cmd_write_baud_rate(struct moschip_port *mos7840_port,
					    int baudRate)
{
	struct usb_serial_port *port = mos7840_port->port;
	int divisor = 0;
	int status;
	__u16 Data;
	__u16 clk_sel_val;

	dev_dbg(&port->dev, "%s - baud = %d\n", __func__, baudRate);
	/* reset clk_uart_sel in spregOffset */
	if (baudRate > 115200) {
#ifdef HW_flow_control
		/* NOTE: need to see the pther register to modify */
		/* setting h/w flow control bit to 1 */
		Data = 0x2b;
		mos7840_port->shadowMCR = Data;
		status = mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER,
									Data);
		if (status < 0) {
			dev_dbg(&port->dev, "Writing spreg failed in set_serial_baud\n");
			return -1;
		}
#endif

	} else {
#ifdef HW_flow_control
		/* setting h/w flow control bit to 0 */
		Data = 0xb;
		mos7840_port->shadowMCR = Data;
		status = mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER,
									Data);
		if (status < 0) {
			dev_dbg(&port->dev, "Writing spreg failed in set_serial_baud\n");
			return -1;
		}
#endif

	}

	if (1) {		/* baudRate <= 115200) */
		clk_sel_val = 0x0;
		Data = 0x0;
		status = mos7840_calc_baud_rate_divisor(port, baudRate, &divisor,
						   &clk_sel_val);
		status = mos7840_get_reg_sync(port, mos7840_port->SpRegOffset,
								 &Data);
		if (status < 0) {
			dev_dbg(&port->dev, "reading spreg failed in set_serial_baud\n");
			return -1;
		}
		Data = (Data & 0x8f) | clk_sel_val;
		status = mos7840_set_reg_sync(port, mos7840_port->SpRegOffset,
								Data);
		if (status < 0) {
			dev_dbg(&port->dev, "Writing spreg failed in set_serial_baud\n");
			return -1;
		}
		/* Calculate the Divisor */

		if (status) {
			dev_err(&port->dev, "%s - bad baud rate\n", __func__);
			return status;
		}
		/* Enable access to divisor latch */
		Data = mos7840_port->shadowLCR | SERIAL_LCR_DLAB;
		mos7840_port->shadowLCR = Data;
		mos7840_set_uart_reg(port, LINE_CONTROL_REGISTER, Data);

		/* Write the divisor */
		Data = (unsigned char)(divisor & 0xff);
		dev_dbg(&port->dev, "set_serial_baud Value to write DLL is %x\n", Data);
		mos7840_set_uart_reg(port, DIVISOR_LATCH_LSB, Data);

		Data = (unsigned char)((divisor & 0xff00) >> 8);
		dev_dbg(&port->dev, "set_serial_baud Value to write DLM is %x\n", Data);
		mos7840_set_uart_reg(port, DIVISOR_LATCH_MSB, Data);

		/* Disable access to divisor latch */
		Data = mos7840_port->shadowLCR & ~SERIAL_LCR_DLAB;
		mos7840_port->shadowLCR = Data;
		mos7840_set_uart_reg(port, LINE_CONTROL_REGISTER, Data);

	}
	return status;
}

/*****************************************************************************
 * mos7840_change_port_settings
 *	This routine is called to set the UART on the device to match
 *      the specified new settings.
 *****************************************************************************/

static void mos7840_change_port_settings(struct tty_struct *tty,
	struct moschip_port *mos7840_port, struct ktermios *old_termios)
{
	struct usb_serial_port *port = mos7840_port->port;
	int baud;
	unsigned cflag;
	__u8 lData;
	__u8 lParity;
	__u8 lStop;
	int status;
	__u16 Data;

	lData = LCR_BITS_8;
	lStop = LCR_STOP_1;
	lParity = LCR_PAR_NONE;

	cflag = tty->termios.c_cflag;

	/* Change the number of bits */
	switch (cflag & CSIZE) {
	case CS5:
		lData = LCR_BITS_5;
		break;

	case CS6:
		lData = LCR_BITS_6;
		break;

	case CS7:
		lData = LCR_BITS_7;
		break;

	default:
	case CS8:
		lData = LCR_BITS_8;
		break;
	}

	/* Change the Parity bit */
	if (cflag & PARENB) {
		if (cflag & PARODD) {
			lParity = LCR_PAR_ODD;
			dev_dbg(&port->dev, "%s - parity = odd\n", __func__);
		} else {
			lParity = LCR_PAR_EVEN;
			dev_dbg(&port->dev, "%s - parity = even\n", __func__);
		}

	} else {
		dev_dbg(&port->dev, "%s - parity = none\n", __func__);
	}

	if (cflag & CMSPAR)
		lParity = lParity | 0x20;

	/* Change the Stop bit */
	if (cflag & CSTOPB) {
		lStop = LCR_STOP_2;
		dev_dbg(&port->dev, "%s - stop bits = 2\n", __func__);
	} else {
		lStop = LCR_STOP_1;
		dev_dbg(&port->dev, "%s - stop bits = 1\n", __func__);
	}

	/* Update the LCR with the correct value */
	mos7840_port->shadowLCR &=
	    ~(LCR_BITS_MASK | LCR_STOP_MASK | LCR_PAR_MASK);
	mos7840_port->shadowLCR |= (lData | lParity | lStop);

	dev_dbg(&port->dev, "%s - mos7840_port->shadowLCR is %x\n", __func__,
		mos7840_port->shadowLCR);
	/* Disable Interrupts */
	Data = 0x00;
	mos7840_set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	Data = 0x00;
	mos7840_set_uart_reg(port, FIFO_CONTROL_REGISTER, Data);

	Data = 0xcf;
	mos7840_set_uart_reg(port, FIFO_CONTROL_REGISTER, Data);

	/* Send the updated LCR value to the mos7840 */
	Data = mos7840_port->shadowLCR;

	mos7840_set_uart_reg(port, LINE_CONTROL_REGISTER, Data);

	Data = 0x00b;
	mos7840_port->shadowMCR = Data;
	mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);
	Data = 0x00b;
	mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);

	/* set up the MCR register and send it to the mos7840 */

	mos7840_port->shadowMCR = MCR_MASTER_IE;
	if (cflag & CBAUD)
		mos7840_port->shadowMCR |= (MCR_DTR | MCR_RTS);

	if (cflag & CRTSCTS)
		mos7840_port->shadowMCR |= (MCR_XON_ANY);
	else
		mos7840_port->shadowMCR &= ~(MCR_XON_ANY);

	Data = mos7840_port->shadowMCR;
	mos7840_set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);

	if (!baud) {
		/* pick a default, any default... */
		dev_dbg(&port->dev, "%s", "Picked default baud...\n");
		baud = 9600;
	}

	dev_dbg(&port->dev, "%s - baud rate = %d\n", __func__, baud);
	status = mos7840_send_cmd_write_baud_rate(mos7840_port, baud);

	/* Enable Interrupts */
	Data = 0x0c;
	mos7840_set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	if (!mos7840_port->read_urb_busy) {
		mos7840_port->read_urb_busy = true;
		status = usb_submit_urb(mos7840_port->read_urb, GFP_KERNEL);
		if (status) {
			dev_dbg(&port->dev, "usb_submit_urb(read bulk) failed, status = %d\n",
			    status);
			mos7840_port->read_urb_busy = false;
		}
	}
	dev_dbg(&port->dev, "%s - mos7840_port->shadowLCR is End %x\n", __func__,
		mos7840_port->shadowLCR);
}

/*****************************************************************************
 * mos7840_set_termios
 *	this function is called by the tty driver when it wants to change
 *	the termios structure
 *****************************************************************************/

static void mos7840_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				struct ktermios *old_termios)
{
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);
	int status;

	/* change the port settings to the new ones specified */

	mos7840_change_port_settings(tty, mos7840_port, old_termios);

	if (!mos7840_port->read_urb_busy) {
		mos7840_port->read_urb_busy = true;
		status = usb_submit_urb(mos7840_port->read_urb, GFP_KERNEL);
		if (status) {
			dev_dbg(&port->dev, "usb_submit_urb(read bulk) failed, status = %d\n",
			    status);
			mos7840_port->read_urb_busy = false;
		}
	}
}

/*****************************************************************************
 * mos7840_get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space.
 *****************************************************************************/

static int mos7840_get_lsr_info(struct tty_struct *tty,
				unsigned int __user *value)
{
	int count;
	unsigned int result = 0;

	count = mos7840_chars_in_buffer(tty);
	if (count == 0)
		result = TIOCSER_TEMT;

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

/*****************************************************************************
 * mos7840_get_serial_info
 *      function to get information about serial port
 *****************************************************************************/

static int mos7840_get_serial_info(struct tty_struct *tty,
				   struct serial_struct *ss)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);

	ss->type = PORT_16550A;
	ss->line = mos7840_port->port->minor;
	ss->port = mos7840_port->port->port_number;
	ss->irq = 0;
	ss->xmit_fifo_size = NUM_URBS * URB_TRANSFER_BUFFER_SIZE;
	ss->baud_base = 9600;
	ss->close_delay = 5 * HZ;
	ss->closing_wait = 30 * HZ;
	return 0;
}

/*****************************************************************************
 * SerialIoctl
 *	this function handles any ioctl calls to the driver
 *****************************************************************************/

static int mos7840_ioctl(struct tty_struct *tty,
			 unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
		/* return number of bytes available */

	case TIOCSERGETLSR:
		dev_dbg(&port->dev, "%s TIOCSERGETLSR\n", __func__);
		return mos7840_get_lsr_info(tty, argp);

	default:
		break;
	}
	return -ENOIOCTLCMD;
}

/*
 * Check if GPO (pin 42) is connected to GPI (pin 33) as recommended by ASIX
 * for MCS7810 by bit-banging a 16-bit word.
 *
 * Note that GPO is really RTS of the third port so this will toggle RTS of
 * port two or three on two- and four-port devices.
 */
static int mos7810_check(struct usb_serial *serial)
{
	int i, pass_count = 0;
	u8 *buf;
	__u16 data = 0, mcr_data = 0;
	__u16 test_pattern = 0x55AA;
	int res;

	buf = kmalloc(VENDOR_READ_LENGTH, GFP_KERNEL);
	if (!buf)
		return 0;	/* failed to identify 7810 */

	/* Store MCR setting */
	res = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
		MCS_RDREQ, MCS_RD_RTYPE, 0x0300, MODEM_CONTROL_REGISTER,
		buf, VENDOR_READ_LENGTH, MOS_WDR_TIMEOUT);
	if (res == VENDOR_READ_LENGTH)
		mcr_data = *buf;

	for (i = 0; i < 16; i++) {
		/* Send the 1-bit test pattern out to MCS7810 test pin */
		usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			MCS_WRREQ, MCS_WR_RTYPE,
			(0x0300 | (((test_pattern >> i) & 0x0001) << 1)),
			MODEM_CONTROL_REGISTER, NULL, 0, MOS_WDR_TIMEOUT);

		/* Read the test pattern back */
		res = usb_control_msg(serial->dev,
				usb_rcvctrlpipe(serial->dev, 0), MCS_RDREQ,
				MCS_RD_RTYPE, 0, GPIO_REGISTER, buf,
				VENDOR_READ_LENGTH, MOS_WDR_TIMEOUT);
		if (res == VENDOR_READ_LENGTH)
			data = *buf;

		/* If this is a MCS7810 device, both test patterns must match */
		if (((test_pattern >> i) ^ (~data >> 1)) & 0x0001)
			break;

		pass_count++;
	}

	/* Restore MCR setting */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0), MCS_WRREQ,
		MCS_WR_RTYPE, 0x0300 | mcr_data, MODEM_CONTROL_REGISTER, NULL,
		0, MOS_WDR_TIMEOUT);

	kfree(buf);

	if (pass_count == 16)
		return 1;

	return 0;
}

static int mos7840_probe(struct usb_serial *serial,
				const struct usb_device_id *id)
{
	unsigned long device_flags = id->driver_info;
	u8 *buf;

	/* Skip device-type detection if we already have device flags. */
	if (device_flags)
		goto out;

	buf = kzalloc(VENDOR_READ_LENGTH, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			MCS_RDREQ, MCS_RD_RTYPE, 0, GPIO_REGISTER, buf,
			VENDOR_READ_LENGTH, MOS_WDR_TIMEOUT);

	/* For a MCS7840 device GPIO0 must be set to 1 */
	if (buf[0] & 0x01)
		device_flags = MCS_PORTS(4);
	else if (mos7810_check(serial))
		device_flags = MCS_PORTS(1) | MCS_LED;
	else
		device_flags = MCS_PORTS(2);

	kfree(buf);
out:
	usb_set_serial_data(serial, (void *)device_flags);

	return 0;
}

static int mos7840_calc_num_ports(struct usb_serial *serial,
					struct usb_serial_endpoints *epds)
{
	unsigned long device_flags = (unsigned long)usb_get_serial_data(serial);
	int num_ports = MCS_PORTS(device_flags);

	if (num_ports == 0 || num_ports > 4)
		return -ENODEV;

	if (epds->num_bulk_in < num_ports || epds->num_bulk_out < num_ports) {
		dev_err(&serial->interface->dev, "missing endpoints\n");
		return -ENODEV;
	}

	return num_ports;
}

static int mos7840_attach(struct usb_serial *serial)
{
	struct device *dev = &serial->interface->dev;
	int status;
	u16 val;

	/* Zero Length flag enable */
	val = 0x0f;
	status = mos7840_set_reg_sync(serial->port[0], ZLP_REG5, val);
	if (status < 0)
		dev_dbg(dev, "Writing ZLP_REG5 failed status-0x%x\n", status);
	else
		dev_dbg(dev, "ZLP_REG5 Writing success status%d\n", status);

	return status;
}

static int mos7840_port_probe(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	unsigned long device_flags = (unsigned long)usb_get_serial_data(serial);
	struct moschip_port *mos7840_port;
	int status;
	int pnum;
	__u16 Data;

	/* we set up the pointers to the endpoints in the mos7840_open *
	 * function, as the structures aren't created yet.             */

	pnum = port->port_number;

	dev_dbg(&port->dev, "mos7840_startup: configuring port %d\n", pnum);
	mos7840_port = kzalloc(sizeof(struct moschip_port), GFP_KERNEL);
	if (!mos7840_port)
		return -ENOMEM;

	/* Initialize all port interrupt end point to port 0 int
	 * endpoint. Our device has only one interrupt end point
	 * common to all port */

	mos7840_port->port = port;
	spin_lock_init(&mos7840_port->pool_lock);

	/* minor is not initialised until later by
	 * usb-serial.c:get_free_serial() and cannot therefore be used
	 * to index device instances */
	mos7840_port->port_num = pnum + 1;
	dev_dbg(&port->dev, "port->minor = %d\n", port->minor);
	dev_dbg(&port->dev, "mos7840_port->port_num = %d\n", mos7840_port->port_num);

	if (mos7840_port->port_num == 1) {
		mos7840_port->SpRegOffset = 0x0;
		mos7840_port->ControlRegOffset = 0x1;
		mos7840_port->DcrRegOffset = 0x4;
	} else {
		u8 phy_num = mos7840_port->port_num;

		/* Port 2 in the 2-port case uses registers of port 3 */
		if (serial->num_ports == 2)
			phy_num = 3;

		mos7840_port->SpRegOffset = 0x8 + 2 * (phy_num - 2);
		mos7840_port->ControlRegOffset = 0x9 + 2 * (phy_num - 2);
		mos7840_port->DcrRegOffset = 0x16 + 3 * (phy_num - 2);
	}
	mos7840_dump_serial_port(port, mos7840_port);
	usb_set_serial_port_data(port, mos7840_port);

	/* enable rx_disable bit in control register */
	status = mos7840_get_reg_sync(port,
			mos7840_port->ControlRegOffset, &Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Reading ControlReg failed status-0x%x\n", status);
		goto error;
	} else
		dev_dbg(&port->dev, "ControlReg Reading success val is %x, status%d\n", Data, status);
	Data |= 0x08;	/* setting driver done bit */
	Data |= 0x04;	/* sp1_bit to have cts change reflect in
			   modem status reg */

	/* Data |= 0x20; //rx_disable bit */
	status = mos7840_set_reg_sync(port,
			mos7840_port->ControlRegOffset, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Writing ControlReg failed(rx_disable) status-0x%x\n", status);
		goto error;
	} else
		dev_dbg(&port->dev, "ControlReg Writing success(rx_disable) status%d\n", status);

	/* Write default values in DCR (i.e 0x01 in DCR0, 0x05 in DCR2
	   and 0x24 in DCR3 */
	Data = 0x01;
	status = mos7840_set_reg_sync(port,
			(__u16) (mos7840_port->DcrRegOffset + 0), Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Writing DCR0 failed status-0x%x\n", status);
		goto error;
	} else
		dev_dbg(&port->dev, "DCR0 Writing success status%d\n", status);

	Data = 0x05;
	status = mos7840_set_reg_sync(port,
			(__u16) (mos7840_port->DcrRegOffset + 1), Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Writing DCR1 failed status-0x%x\n", status);
		goto error;
	} else
		dev_dbg(&port->dev, "DCR1 Writing success status%d\n", status);

	Data = 0x24;
	status = mos7840_set_reg_sync(port,
			(__u16) (mos7840_port->DcrRegOffset + 2), Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Writing DCR2 failed status-0x%x\n", status);
		goto error;
	} else
		dev_dbg(&port->dev, "DCR2 Writing success status%d\n", status);

	/* write values in clkstart0x0 and clkmulti 0x20 */
	Data = 0x0;
	status = mos7840_set_reg_sync(port, CLK_START_VALUE_REGISTER, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Writing CLK_START_VALUE_REGISTER failed status-0x%x\n", status);
		goto error;
	} else
		dev_dbg(&port->dev, "CLK_START_VALUE_REGISTER Writing success status%d\n", status);

	Data = 0x20;
	status = mos7840_set_reg_sync(port, CLK_MULTI_REGISTER, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Writing CLK_MULTI_REGISTER failed status-0x%x\n", status);
		goto error;
	} else
		dev_dbg(&port->dev, "CLK_MULTI_REGISTER Writing success status%d\n", status);

	/* write value 0x0 to scratchpad register */
	Data = 0x00;
	status = mos7840_set_uart_reg(port, SCRATCH_PAD_REGISTER, Data);
	if (status < 0) {
		dev_dbg(&port->dev, "Writing SCRATCH_PAD_REGISTER failed status-0x%x\n", status);
		goto error;
	} else
		dev_dbg(&port->dev, "SCRATCH_PAD_REGISTER Writing success status%d\n", status);

	/* Zero Length flag register */
	if ((mos7840_port->port_num != 1) && (serial->num_ports == 2)) {
		Data = 0xff;
		status = mos7840_set_reg_sync(port,
				(__u16) (ZLP_REG1 +
					((__u16)mos7840_port->port_num)), Data);
		dev_dbg(&port->dev, "ZLIP offset %x\n",
				(__u16)(ZLP_REG1 + ((__u16) mos7840_port->port_num)));
		if (status < 0) {
			dev_dbg(&port->dev, "Writing ZLP_REG%d failed status-0x%x\n", pnum + 2, status);
			goto error;
		} else
			dev_dbg(&port->dev, "ZLP_REG%d Writing success status%d\n", pnum + 2, status);
	} else {
		Data = 0xff;
		status = mos7840_set_reg_sync(port,
				(__u16) (ZLP_REG1 +
					((__u16)mos7840_port->port_num) - 0x1), Data);
		dev_dbg(&port->dev, "ZLIP offset %x\n",
				(__u16)(ZLP_REG1 + ((__u16) mos7840_port->port_num) - 0x1));
		if (status < 0) {
			dev_dbg(&port->dev, "Writing ZLP_REG%d failed status-0x%x\n", pnum + 1, status);
			goto error;
		} else
			dev_dbg(&port->dev, "ZLP_REG%d Writing success status%d\n", pnum + 1, status);

	}

	mos7840_port->has_led = device_flags & MCS_LED;

	/* Initialize LED timers */
	if (mos7840_port->has_led) {
		mos7840_port->led_urb = usb_alloc_urb(0, GFP_KERNEL);
		mos7840_port->led_dr = kmalloc(sizeof(*mos7840_port->led_dr),
								GFP_KERNEL);
		if (!mos7840_port->led_urb || !mos7840_port->led_dr) {
			status = -ENOMEM;
			goto error;
		}

		timer_setup(&mos7840_port->led_timer1, mos7840_led_off, 0);
		mos7840_port->led_timer1.expires =
			jiffies + msecs_to_jiffies(LED_ON_MS);
		timer_setup(&mos7840_port->led_timer2, mos7840_led_flag_off,
			    0);
		mos7840_port->led_timer2.expires =
			jiffies + msecs_to_jiffies(LED_OFF_MS);

		/* Turn off LED */
		mos7840_set_led_sync(port, MODEM_CONTROL_REGISTER, 0x0300);
	}

	return 0;
error:
	kfree(mos7840_port->led_dr);
	usb_free_urb(mos7840_port->led_urb);
	kfree(mos7840_port);

	return status;
}

static int mos7840_port_remove(struct usb_serial_port *port)
{
	struct moschip_port *mos7840_port = usb_get_serial_port_data(port);

	if (mos7840_port->has_led) {
		/* Turn off LED */
		mos7840_set_led_sync(port, MODEM_CONTROL_REGISTER, 0x0300);

		del_timer_sync(&mos7840_port->led_timer1);
		del_timer_sync(&mos7840_port->led_timer2);

		usb_kill_urb(mos7840_port->led_urb);
		usb_free_urb(mos7840_port->led_urb);
		kfree(mos7840_port->led_dr);
	}

	kfree(mos7840_port);

	return 0;
}

static struct usb_serial_driver moschip7840_4port_device = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "mos7840",
		   },
	.description = DRIVER_DESC,
	.id_table = id_table,
	.num_interrupt_in = 1,
	.open = mos7840_open,
	.close = mos7840_close,
	.write = mos7840_write,
	.write_room = mos7840_write_room,
	.chars_in_buffer = mos7840_chars_in_buffer,
	.throttle = mos7840_throttle,
	.unthrottle = mos7840_unthrottle,
	.calc_num_ports = mos7840_calc_num_ports,
	.probe = mos7840_probe,
	.attach = mos7840_attach,
	.ioctl = mos7840_ioctl,
	.get_serial = mos7840_get_serial_info,
	.set_termios = mos7840_set_termios,
	.break_ctl = mos7840_break,
	.tiocmget = mos7840_tiocmget,
	.tiocmset = mos7840_tiocmset,
	.get_icount = usb_serial_generic_get_icount,
	.port_probe = mos7840_port_probe,
	.port_remove = mos7840_port_remove,
	.read_bulk_callback = mos7840_bulk_in_callback,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&moschip7840_4port_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
