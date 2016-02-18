/*
 * USB Moxa UPORT 11x0 Serial Driver
 *
 * Copyright (C) 2007 MOXA Technologies Co., Ltd.
 * Copyright (C) 2015 Mathieu Othacehe <m.othacehe@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 * Supports the following Moxa USB to serial converters:
 *  UPort 1110,  1 port RS-232 USB to Serial Hub.
 *  UPort 1130,  1 port RS-422/485 USB to Serial Hub.
 *  UPort 1130I, 1 port RS-422/485 USB to Serial Hub with isolation
 *    protection.
 *  UPort 1150,  1 port RS-232/422/485 USB to Serial Hub.
 *  UPort 1150I, 1 port RS-232/422/485 USB to Serial Hub with isolation
 *  protection.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

/* Vendor and product ids */
#define MXU1_VENDOR_ID				0x110a
#define MXU1_1110_PRODUCT_ID			0x1110
#define MXU1_1130_PRODUCT_ID			0x1130
#define MXU1_1150_PRODUCT_ID			0x1150
#define MXU1_1151_PRODUCT_ID			0x1151
#define MXU1_1131_PRODUCT_ID			0x1131

/* Commands */
#define MXU1_GET_VERSION			0x01
#define MXU1_GET_PORT_STATUS			0x02
#define MXU1_GET_PORT_DEV_INFO			0x03
#define MXU1_GET_CONFIG				0x04
#define MXU1_SET_CONFIG				0x05
#define MXU1_OPEN_PORT				0x06
#define MXU1_CLOSE_PORT				0x07
#define MXU1_START_PORT				0x08
#define MXU1_STOP_PORT				0x09
#define MXU1_TEST_PORT				0x0A
#define MXU1_PURGE_PORT				0x0B
#define MXU1_RESET_EXT_DEVICE			0x0C
#define MXU1_GET_OUTQUEUE			0x0D
#define MXU1_WRITE_DATA				0x80
#define MXU1_READ_DATA				0x81
#define MXU1_REQ_TYPE_CLASS			0x82

/* Module identifiers */
#define MXU1_I2C_PORT				0x01
#define MXU1_IEEE1284_PORT			0x02
#define MXU1_UART1_PORT				0x03
#define MXU1_UART2_PORT				0x04
#define MXU1_RAM_PORT				0x05

/* Modem status */
#define MXU1_MSR_DELTA_CTS			0x01
#define MXU1_MSR_DELTA_DSR			0x02
#define MXU1_MSR_DELTA_RI			0x04
#define MXU1_MSR_DELTA_CD			0x08
#define MXU1_MSR_CTS				0x10
#define MXU1_MSR_DSR				0x20
#define MXU1_MSR_RI				0x40
#define MXU1_MSR_CD				0x80
#define MXU1_MSR_DELTA_MASK			0x0F
#define MXU1_MSR_MASK				0xF0

/* Line status */
#define MXU1_LSR_OVERRUN_ERROR			0x01
#define MXU1_LSR_PARITY_ERROR			0x02
#define MXU1_LSR_FRAMING_ERROR			0x04
#define MXU1_LSR_BREAK				0x08
#define MXU1_LSR_ERROR				0x0F
#define MXU1_LSR_RX_FULL			0x10
#define MXU1_LSR_TX_EMPTY			0x20

/* Modem control */
#define MXU1_MCR_LOOP				0x04
#define MXU1_MCR_DTR				0x10
#define MXU1_MCR_RTS				0x20

/* Mask settings */
#define MXU1_UART_ENABLE_RTS_IN			0x0001
#define MXU1_UART_DISABLE_RTS			0x0002
#define MXU1_UART_ENABLE_PARITY_CHECKING	0x0008
#define MXU1_UART_ENABLE_DSR_OUT		0x0010
#define MXU1_UART_ENABLE_CTS_OUT		0x0020
#define MXU1_UART_ENABLE_X_OUT			0x0040
#define MXU1_UART_ENABLE_XA_OUT			0x0080
#define MXU1_UART_ENABLE_X_IN			0x0100
#define MXU1_UART_ENABLE_DTR_IN			0x0800
#define MXU1_UART_DISABLE_DTR			0x1000
#define MXU1_UART_ENABLE_MS_INTS		0x2000
#define MXU1_UART_ENABLE_AUTO_START_DMA		0x4000
#define MXU1_UART_SEND_BREAK_SIGNAL		0x8000

/* Parity */
#define MXU1_UART_NO_PARITY			0x00
#define MXU1_UART_ODD_PARITY			0x01
#define MXU1_UART_EVEN_PARITY			0x02
#define MXU1_UART_MARK_PARITY			0x03
#define MXU1_UART_SPACE_PARITY			0x04

/* Stop bits */
#define MXU1_UART_1_STOP_BITS			0x00
#define MXU1_UART_1_5_STOP_BITS			0x01
#define MXU1_UART_2_STOP_BITS			0x02

/* Bits per character */
#define MXU1_UART_5_DATA_BITS			0x00
#define MXU1_UART_6_DATA_BITS			0x01
#define MXU1_UART_7_DATA_BITS			0x02
#define MXU1_UART_8_DATA_BITS			0x03

/* Operation modes */
#define MXU1_UART_232				0x00
#define MXU1_UART_485_RECEIVER_DISABLED		0x01
#define MXU1_UART_485_RECEIVER_ENABLED		0x02

/* Pipe transfer mode and timeout */
#define MXU1_PIPE_MODE_CONTINUOUS		0x01
#define MXU1_PIPE_MODE_MASK			0x03
#define MXU1_PIPE_TIMEOUT_MASK			0x7C
#define MXU1_PIPE_TIMEOUT_ENABLE		0x80

/* Config struct */
struct mxu1_uart_config {
	__be16	wBaudRate;
	__be16	wFlags;
	u8	bDataBits;
	u8	bParity;
	u8	bStopBits;
	char	cXon;
	char	cXoff;
	u8	bUartMode;
} __packed;

/* Purge modes */
#define MXU1_PURGE_OUTPUT			0x00
#define MXU1_PURGE_INPUT			0x80

/* Read/Write data */
#define MXU1_RW_DATA_ADDR_SFR			0x10
#define MXU1_RW_DATA_ADDR_IDATA			0x20
#define MXU1_RW_DATA_ADDR_XDATA			0x30
#define MXU1_RW_DATA_ADDR_CODE			0x40
#define MXU1_RW_DATA_ADDR_GPIO			0x50
#define MXU1_RW_DATA_ADDR_I2C			0x60
#define MXU1_RW_DATA_ADDR_FLASH			0x70
#define MXU1_RW_DATA_ADDR_DSP			0x80

#define MXU1_RW_DATA_UNSPECIFIED		0x00
#define MXU1_RW_DATA_BYTE			0x01
#define MXU1_RW_DATA_WORD			0x02
#define MXU1_RW_DATA_DOUBLE_WORD		0x04

struct mxu1_write_data_bytes {
	u8	bAddrType;
	u8	bDataType;
	u8	bDataCounter;
	__be16	wBaseAddrHi;
	__be16	wBaseAddrLo;
	u8	bData[0];
} __packed;

/* Interrupt codes */
#define MXU1_CODE_HARDWARE_ERROR		0xFF
#define MXU1_CODE_DATA_ERROR			0x03
#define MXU1_CODE_MODEM_STATUS			0x04

static inline int mxu1_get_func_from_code(unsigned char code)
{
	return code & 0x0f;
}

/* Download firmware max packet size */
#define MXU1_DOWNLOAD_MAX_PACKET_SIZE		64

/* Firmware image header */
struct mxu1_firmware_header {
	__le16 wLength;
	u8 bCheckSum;
} __packed;

#define MXU1_UART_BASE_ADDR	    0xFFA0
#define MXU1_UART_OFFSET_MCR	    0x0004

#define MXU1_BAUD_BASE              923077

#define MXU1_TRANSFER_TIMEOUT	    2
#define MXU1_DOWNLOAD_TIMEOUT       1000
#define MXU1_DEFAULT_CLOSING_WAIT   4000 /* in .01 secs */

struct mxu1_port {
	u8 msr;
	u8 mcr;
	u8 uart_mode;
	spinlock_t spinlock; /* Protects msr */
	struct mutex mutex; /* Protects mcr */
	bool send_break;
};

struct mxu1_device {
	u16 mxd_model;
};

static const struct usb_device_id mxu1_idtable[] = {
	{ USB_DEVICE(MXU1_VENDOR_ID, MXU1_1110_PRODUCT_ID) },
	{ USB_DEVICE(MXU1_VENDOR_ID, MXU1_1130_PRODUCT_ID) },
	{ USB_DEVICE(MXU1_VENDOR_ID, MXU1_1150_PRODUCT_ID) },
	{ USB_DEVICE(MXU1_VENDOR_ID, MXU1_1151_PRODUCT_ID) },
	{ USB_DEVICE(MXU1_VENDOR_ID, MXU1_1131_PRODUCT_ID) },
	{ }
};

MODULE_DEVICE_TABLE(usb, mxu1_idtable);

/* Write the given buffer out to the control pipe.  */
static int mxu1_send_ctrl_data_urb(struct usb_serial *serial,
				   u8 request,
				   u16 value, u16 index,
				   void *data, size_t size)
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
			"%s - usb_control_msg failed: %d\n",
			__func__, status);
		return status;
	}

	if (status != size) {
		dev_err(&serial->interface->dev,
			"%s - short write (%d / %zd)\n",
			__func__, status, size);
		return -EIO;
	}

	return 0;
}

/* Send a vendor request without any data */
static int mxu1_send_ctrl_urb(struct usb_serial *serial,
			      u8 request, u16 value, u16 index)
{
	return mxu1_send_ctrl_data_urb(serial, request, value, index,
				       NULL, 0);
}

static int mxu1_download_firmware(struct usb_serial *serial,
				  const struct firmware *fw_p)
{
	int status = 0;
	int buffer_size;
	int pos;
	int len;
	int done;
	u8 cs = 0;
	u8 *buffer;
	struct usb_device *dev = serial->dev;
	struct mxu1_firmware_header *header;
	unsigned int pipe;

	pipe = usb_sndbulkpipe(dev, serial->port[0]->bulk_out_endpointAddress);

	buffer_size = fw_p->size + sizeof(*header);
	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	memcpy(buffer, fw_p->data, fw_p->size);
	memset(buffer + fw_p->size, 0xff, buffer_size - fw_p->size);

	for (pos = sizeof(*header); pos < buffer_size; pos++)
		cs = (u8)(cs + buffer[pos]);

	header = (struct mxu1_firmware_header *)buffer;
	header->wLength = cpu_to_le16(buffer_size - sizeof(*header));
	header->bCheckSum = cs;

	dev_dbg(&dev->dev, "%s - downloading firmware\n", __func__);

	for (pos = 0; pos < buffer_size; pos += done) {
		len = min(buffer_size - pos, MXU1_DOWNLOAD_MAX_PACKET_SIZE);

		status = usb_bulk_msg(dev, pipe, buffer + pos, len, &done,
				MXU1_DOWNLOAD_TIMEOUT);
		if (status)
			break;
	}

	kfree(buffer);

	if (status) {
		dev_err(&dev->dev, "failed to download firmware: %d\n", status);
		return status;
	}

	msleep_interruptible(100);
	usb_reset_device(dev);

	dev_dbg(&dev->dev, "%s - download successful\n", __func__);

	return 0;
}

static int mxu1_port_probe(struct usb_serial_port *port)
{
	struct mxu1_port *mxport;
	struct mxu1_device *mxdev;

	if (!port->interrupt_in_urb) {
		dev_err(&port->dev, "no interrupt urb\n");
		return -ENODEV;
	}

	mxport = kzalloc(sizeof(struct mxu1_port), GFP_KERNEL);
	if (!mxport)
		return -ENOMEM;

	spin_lock_init(&mxport->spinlock);
	mutex_init(&mxport->mutex);

	mxdev = usb_get_serial_data(port->serial);

	switch (mxdev->mxd_model) {
	case MXU1_1110_PRODUCT_ID:
	case MXU1_1150_PRODUCT_ID:
	case MXU1_1151_PRODUCT_ID:
		mxport->uart_mode = MXU1_UART_232;
		break;
	case MXU1_1130_PRODUCT_ID:
	case MXU1_1131_PRODUCT_ID:
		mxport->uart_mode = MXU1_UART_485_RECEIVER_DISABLED;
		break;
	}

	usb_set_serial_port_data(port, mxport);

	port->port.closing_wait =
			msecs_to_jiffies(MXU1_DEFAULT_CLOSING_WAIT * 10);
	port->port.drain_delay = 1;

	return 0;
}

static int mxu1_port_remove(struct usb_serial_port *port)
{
	struct mxu1_port *mxport;

	mxport = usb_get_serial_port_data(port);
	kfree(mxport);

	return 0;
}

static int mxu1_startup(struct usb_serial *serial)
{
	struct mxu1_device *mxdev;
	struct usb_device *dev = serial->dev;
	struct usb_host_interface *cur_altsetting;
	char fw_name[32];
	const struct firmware *fw_p = NULL;
	int err;

	dev_dbg(&serial->interface->dev, "%s - product 0x%04X, num configurations %d, configuration value %d\n",
		__func__, le16_to_cpu(dev->descriptor.idProduct),
		dev->descriptor.bNumConfigurations,
		dev->actconfig->desc.bConfigurationValue);

	/* create device structure */
	mxdev = kzalloc(sizeof(struct mxu1_device), GFP_KERNEL);
	if (!mxdev)
		return -ENOMEM;

	usb_set_serial_data(serial, mxdev);

	mxdev->mxd_model = le16_to_cpu(dev->descriptor.idProduct);

	cur_altsetting = serial->interface->cur_altsetting;

	/* if we have only 1 configuration, download firmware */
	if (cur_altsetting->desc.bNumEndpoints == 1) {

		snprintf(fw_name,
			 sizeof(fw_name),
			 "moxa/moxa-%04x.fw",
			 mxdev->mxd_model);

		err = request_firmware(&fw_p, fw_name, &serial->interface->dev);
		if (err) {
			dev_err(&serial->interface->dev, "failed to request firmware: %d\n",
				err);
			goto err_free_mxdev;
		}

		err = mxu1_download_firmware(serial, fw_p);
		if (err)
			goto err_release_firmware;

		/* device is being reset */
		err = -ENODEV;
		goto err_release_firmware;
	}

	return 0;

err_release_firmware:
	release_firmware(fw_p);
err_free_mxdev:
	kfree(mxdev);

	return err;
}

static void mxu1_release(struct usb_serial *serial)
{
	struct mxu1_device *mxdev;

	mxdev = usb_get_serial_data(serial);
	kfree(mxdev);
}

static int mxu1_write_byte(struct usb_serial_port *port, u32 addr,
			   u8 mask, u8 byte)
{
	int status;
	size_t size;
	struct mxu1_write_data_bytes *data;

	dev_dbg(&port->dev, "%s - addr 0x%08X, mask 0x%02X, byte 0x%02X\n",
		__func__, addr, mask, byte);

	size = sizeof(struct mxu1_write_data_bytes) + 2;
	data = kzalloc(size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->bAddrType = MXU1_RW_DATA_ADDR_XDATA;
	data->bDataType = MXU1_RW_DATA_BYTE;
	data->bDataCounter = 1;
	data->wBaseAddrHi = cpu_to_be16(addr >> 16);
	data->wBaseAddrLo = cpu_to_be16(addr);
	data->bData[0] = mask;
	data->bData[1] = byte;

	status = mxu1_send_ctrl_data_urb(port->serial, MXU1_WRITE_DATA, 0,
					 MXU1_RAM_PORT, data, size);
	if (status < 0)
		dev_err(&port->dev, "%s - failed: %d\n", __func__, status);

	kfree(data);

	return status;
}

static int mxu1_set_mcr(struct usb_serial_port *port, unsigned int mcr)
{
	int status;

	status = mxu1_write_byte(port,
				 MXU1_UART_BASE_ADDR + MXU1_UART_OFFSET_MCR,
				 MXU1_MCR_RTS | MXU1_MCR_DTR | MXU1_MCR_LOOP,
				 mcr);
	return status;
}

static void mxu1_set_termios(struct tty_struct *tty,
			     struct usb_serial_port *port,
			     struct ktermios *old_termios)
{
	struct mxu1_port *mxport = usb_get_serial_port_data(port);
	struct mxu1_uart_config *config;
	tcflag_t cflag, iflag;
	speed_t baud;
	int status;
	unsigned int mcr;

	cflag = tty->termios.c_cflag;
	iflag = tty->termios.c_iflag;

	if (old_termios &&
	    !tty_termios_hw_change(&tty->termios, old_termios) &&
	    tty->termios.c_iflag == old_termios->c_iflag) {
		dev_dbg(&port->dev, "%s - nothing to change\n", __func__);
		return;
	}

	dev_dbg(&port->dev,
		"%s - cflag 0x%08x, iflag 0x%08x\n", __func__, cflag, iflag);

	if (old_termios) {
		dev_dbg(&port->dev, "%s - old cflag 0x%08x, old iflag 0x%08x\n",
			__func__,
			old_termios->c_cflag,
			old_termios->c_iflag);
	}

	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config)
		return;

	/* these flags must be set */
	config->wFlags |= MXU1_UART_ENABLE_MS_INTS;
	config->wFlags |= MXU1_UART_ENABLE_AUTO_START_DMA;
	if (mxport->send_break)
		config->wFlags |= MXU1_UART_SEND_BREAK_SIGNAL;
	config->bUartMode = mxport->uart_mode;

	switch (C_CSIZE(tty)) {
	case CS5:
		config->bDataBits = MXU1_UART_5_DATA_BITS;
		break;
	case CS6:
		config->bDataBits = MXU1_UART_6_DATA_BITS;
		break;
	case CS7:
		config->bDataBits = MXU1_UART_7_DATA_BITS;
		break;
	default:
	case CS8:
		config->bDataBits = MXU1_UART_8_DATA_BITS;
		break;
	}

	if (C_PARENB(tty)) {
		config->wFlags |= MXU1_UART_ENABLE_PARITY_CHECKING;
		if (C_CMSPAR(tty)) {
			if (C_PARODD(tty))
				config->bParity = MXU1_UART_MARK_PARITY;
			else
				config->bParity = MXU1_UART_SPACE_PARITY;
		} else {
			if (C_PARODD(tty))
				config->bParity = MXU1_UART_ODD_PARITY;
			else
				config->bParity = MXU1_UART_EVEN_PARITY;
		}
	} else {
		config->bParity = MXU1_UART_NO_PARITY;
	}

	if (C_CSTOPB(tty))
		config->bStopBits = MXU1_UART_2_STOP_BITS;
	else
		config->bStopBits = MXU1_UART_1_STOP_BITS;

	if (C_CRTSCTS(tty)) {
		/* RTS flow control must be off to drop RTS for baud rate B0 */
		if (C_BAUD(tty) != B0)
			config->wFlags |= MXU1_UART_ENABLE_RTS_IN;
		config->wFlags |= MXU1_UART_ENABLE_CTS_OUT;
	}

	if (I_IXOFF(tty) || I_IXON(tty)) {
		config->cXon  = START_CHAR(tty);
		config->cXoff = STOP_CHAR(tty);

		if (I_IXOFF(tty))
			config->wFlags |= MXU1_UART_ENABLE_X_IN;

		if (I_IXON(tty))
			config->wFlags |= MXU1_UART_ENABLE_X_OUT;
	}

	baud = tty_get_baud_rate(tty);
	if (!baud)
		baud = 9600;
	config->wBaudRate = MXU1_BAUD_BASE / baud;

	dev_dbg(&port->dev, "%s - BaudRate=%d, wBaudRate=%d, wFlags=0x%04X, bDataBits=%d, bParity=%d, bStopBits=%d, cXon=%d, cXoff=%d, bUartMode=%d\n",
		__func__, baud, config->wBaudRate, config->wFlags,
		config->bDataBits, config->bParity, config->bStopBits,
		config->cXon, config->cXoff, config->bUartMode);

	cpu_to_be16s(&config->wBaudRate);
	cpu_to_be16s(&config->wFlags);

	status = mxu1_send_ctrl_data_urb(port->serial, MXU1_SET_CONFIG, 0,
					 MXU1_UART1_PORT, config,
					 sizeof(*config));
	if (status)
		dev_err(&port->dev, "cannot set config: %d\n", status);

	mutex_lock(&mxport->mutex);
	mcr = mxport->mcr;

	if (C_BAUD(tty) == B0)
		mcr &= ~(MXU1_MCR_DTR | MXU1_MCR_RTS);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		mcr |= MXU1_MCR_DTR | MXU1_MCR_RTS;

	status = mxu1_set_mcr(port, mcr);
	if (status)
		dev_err(&port->dev, "cannot set modem control: %d\n", status);
	else
		mxport->mcr = mcr;

	mutex_unlock(&mxport->mutex);

	kfree(config);
}

static int mxu1_get_serial_info(struct usb_serial_port *port,
				struct serial_struct __user *ret_arg)
{
	struct serial_struct ret_serial;
	unsigned cwait;

	if (!ret_arg)
		return -EFAULT;

	cwait = port->port.closing_wait;
	if (cwait != ASYNC_CLOSING_WAIT_NONE)
		cwait = jiffies_to_msecs(cwait) / 10;

	memset(&ret_serial, 0, sizeof(ret_serial));

	ret_serial.type = PORT_16550A;
	ret_serial.line = port->minor;
	ret_serial.port = 0;
	ret_serial.xmit_fifo_size = port->bulk_out_size;
	ret_serial.baud_base = MXU1_BAUD_BASE;
	ret_serial.close_delay = 5*HZ;
	ret_serial.closing_wait = cwait;

	if (copy_to_user(ret_arg, &ret_serial, sizeof(*ret_arg)))
		return -EFAULT;

	return 0;
}


static int mxu1_set_serial_info(struct usb_serial_port *port,
				struct serial_struct __user *new_arg)
{
	struct serial_struct new_serial;
	unsigned cwait;

	if (copy_from_user(&new_serial, new_arg, sizeof(new_serial)))
		return -EFAULT;

	cwait = new_serial.closing_wait;
	if (cwait != ASYNC_CLOSING_WAIT_NONE)
		cwait = msecs_to_jiffies(10 * new_serial.closing_wait);

	port->port.closing_wait = cwait;

	return 0;
}

static int mxu1_ioctl(struct tty_struct *tty,
		      unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;

	switch (cmd) {
	case TIOCGSERIAL:
		return mxu1_get_serial_info(port,
					    (struct serial_struct __user *)arg);
	case TIOCSSERIAL:
		return mxu1_set_serial_info(port,
					    (struct serial_struct __user *)arg);
	}

	return -ENOIOCTLCMD;
}

static int mxu1_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mxu1_port *mxport = usb_get_serial_port_data(port);
	unsigned int result;
	unsigned int msr;
	unsigned int mcr;
	unsigned long flags;

	mutex_lock(&mxport->mutex);
	spin_lock_irqsave(&mxport->spinlock, flags);

	msr = mxport->msr;
	mcr = mxport->mcr;

	spin_unlock_irqrestore(&mxport->spinlock, flags);
	mutex_unlock(&mxport->mutex);

	result = ((mcr & MXU1_MCR_DTR)	? TIOCM_DTR	: 0) |
		 ((mcr & MXU1_MCR_RTS)	? TIOCM_RTS	: 0) |
		 ((mcr & MXU1_MCR_LOOP) ? TIOCM_LOOP	: 0) |
		 ((msr & MXU1_MSR_CTS)	? TIOCM_CTS	: 0) |
		 ((msr & MXU1_MSR_CD)	? TIOCM_CAR	: 0) |
		 ((msr & MXU1_MSR_RI)	? TIOCM_RI	: 0) |
		 ((msr & MXU1_MSR_DSR)	? TIOCM_DSR	: 0);

	dev_dbg(&port->dev, "%s - 0x%04X\n", __func__, result);

	return result;
}

static int mxu1_tiocmset(struct tty_struct *tty,
			 unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mxu1_port *mxport = usb_get_serial_port_data(port);
	int err;
	unsigned int mcr;

	mutex_lock(&mxport->mutex);
	mcr = mxport->mcr;

	if (set & TIOCM_RTS)
		mcr |= MXU1_MCR_RTS;
	if (set & TIOCM_DTR)
		mcr |= MXU1_MCR_DTR;
	if (set & TIOCM_LOOP)
		mcr |= MXU1_MCR_LOOP;

	if (clear & TIOCM_RTS)
		mcr &= ~MXU1_MCR_RTS;
	if (clear & TIOCM_DTR)
		mcr &= ~MXU1_MCR_DTR;
	if (clear & TIOCM_LOOP)
		mcr &= ~MXU1_MCR_LOOP;

	err = mxu1_set_mcr(port, mcr);
	if (!err)
		mxport->mcr = mcr;

	mutex_unlock(&mxport->mutex);

	return err;
}

static void mxu1_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct mxu1_port *mxport = usb_get_serial_port_data(port);

	if (break_state == -1)
		mxport->send_break = true;
	else
		mxport->send_break = false;

	mxu1_set_termios(tty, port, NULL);
}

static int mxu1_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct mxu1_port *mxport = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	int status;
	u16 open_settings;

	open_settings = (MXU1_PIPE_MODE_CONTINUOUS |
			 MXU1_PIPE_TIMEOUT_ENABLE |
			 (MXU1_TRANSFER_TIMEOUT << 2));

	mxport->msr = 0;

	status = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (status) {
		dev_err(&port->dev, "failed to submit interrupt urb: %d\n",
			status);
		return status;
	}

	if (tty)
		mxu1_set_termios(tty, port, NULL);

	status = mxu1_send_ctrl_urb(serial, MXU1_OPEN_PORT,
				    open_settings, MXU1_UART1_PORT);
	if (status) {
		dev_err(&port->dev, "cannot send open command: %d\n", status);
		goto unlink_int_urb;
	}

	status = mxu1_send_ctrl_urb(serial, MXU1_START_PORT,
				    0, MXU1_UART1_PORT);
	if (status) {
		dev_err(&port->dev, "cannot send start command: %d\n", status);
		goto unlink_int_urb;
	}

	status = mxu1_send_ctrl_urb(serial, MXU1_PURGE_PORT,
				    MXU1_PURGE_INPUT, MXU1_UART1_PORT);
	if (status) {
		dev_err(&port->dev, "cannot clear input buffers: %d\n",
			status);

		goto unlink_int_urb;
	}

	status = mxu1_send_ctrl_urb(serial, MXU1_PURGE_PORT,
				    MXU1_PURGE_OUTPUT, MXU1_UART1_PORT);
	if (status) {
		dev_err(&port->dev, "cannot clear output buffers: %d\n",
			status);

		goto unlink_int_urb;
	}

	/*
	 * reset the data toggle on the bulk endpoints to work around bug in
	 * host controllers where things get out of sync some times
	 */
	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

	if (tty)
		mxu1_set_termios(tty, port, NULL);

	status = mxu1_send_ctrl_urb(serial, MXU1_OPEN_PORT,
				    open_settings, MXU1_UART1_PORT);
	if (status) {
		dev_err(&port->dev, "cannot send open command: %d\n", status);
		goto unlink_int_urb;
	}

	status = mxu1_send_ctrl_urb(serial, MXU1_START_PORT,
				    0, MXU1_UART1_PORT);
	if (status) {
		dev_err(&port->dev, "cannot send start command: %d\n", status);
		goto unlink_int_urb;
	}

	status = usb_serial_generic_open(tty, port);
	if (status)
		goto unlink_int_urb;

	return 0;

unlink_int_urb:
	usb_kill_urb(port->interrupt_in_urb);

	return status;
}

static void mxu1_close(struct usb_serial_port *port)
{
	int status;

	usb_serial_generic_close(port);
	usb_kill_urb(port->interrupt_in_urb);

	status = mxu1_send_ctrl_urb(port->serial, MXU1_CLOSE_PORT,
				    0, MXU1_UART1_PORT);
	if (status) {
		dev_err(&port->dev, "failed to send close port command: %d\n",
			status);
	}
}

static void mxu1_handle_new_msr(struct usb_serial_port *port, u8 msr)
{
	struct mxu1_port *mxport = usb_get_serial_port_data(port);
	struct async_icount *icount;
	unsigned long flags;

	dev_dbg(&port->dev, "%s - msr 0x%02X\n", __func__, msr);

	spin_lock_irqsave(&mxport->spinlock, flags);
	mxport->msr = msr & MXU1_MSR_MASK;
	spin_unlock_irqrestore(&mxport->spinlock, flags);

	if (msr & MXU1_MSR_DELTA_MASK) {
		icount = &port->icount;
		if (msr & MXU1_MSR_DELTA_CTS)
			icount->cts++;
		if (msr & MXU1_MSR_DELTA_DSR)
			icount->dsr++;
		if (msr & MXU1_MSR_DELTA_CD)
			icount->dcd++;
		if (msr & MXU1_MSR_DELTA_RI)
			icount->rng++;

		wake_up_interruptible(&port->port.delta_msr_wait);
	}
}

static void mxu1_interrupt_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	int length = urb->actual_length;
	int function;
	int status;
	u8 msr;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		dev_dbg(&port->dev, "%s - urb shutting down: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(&port->dev, "%s - nonzero urb status: %d\n",
			__func__, urb->status);
		goto exit;
	}

	if (length != 2) {
		dev_dbg(&port->dev, "%s - bad packet size: %d\n",
			__func__, length);
		goto exit;
	}

	if (data[0] == MXU1_CODE_HARDWARE_ERROR) {
		dev_err(&port->dev, "hardware error: %d\n", data[1]);
		goto exit;
	}

	function = mxu1_get_func_from_code(data[0]);

	dev_dbg(&port->dev, "%s - function %d, data 0x%02X\n",
		 __func__, function, data[1]);

	switch (function) {
	case MXU1_CODE_DATA_ERROR:
		dev_dbg(&port->dev, "%s - DATA ERROR, data 0x%02X\n",
			 __func__, data[1]);
		break;

	case MXU1_CODE_MODEM_STATUS:
		msr = data[1];
		mxu1_handle_new_msr(port, msr);
		break;

	default:
		dev_err(&port->dev, "unknown interrupt code: 0x%02X\n",
			data[1]);
		break;
	}

exit:
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_err(&port->dev, "resubmit interrupt urb failed: %d\n",
			status);
	}
}

static struct usb_serial_driver mxu11x0_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "mxu11x0",
	},
	.description		= "MOXA UPort 11x0",
	.id_table		= mxu1_idtable,
	.num_ports		= 1,
	.port_probe             = mxu1_port_probe,
	.port_remove            = mxu1_port_remove,
	.attach			= mxu1_startup,
	.release                = mxu1_release,
	.open			= mxu1_open,
	.close			= mxu1_close,
	.ioctl			= mxu1_ioctl,
	.set_termios		= mxu1_set_termios,
	.tiocmget		= mxu1_tiocmget,
	.tiocmset		= mxu1_tiocmset,
	.tiocmiwait		= usb_serial_generic_tiocmiwait,
	.get_icount		= usb_serial_generic_get_icount,
	.break_ctl		= mxu1_break,
	.read_int_callback	= mxu1_interrupt_callback,
};

static struct usb_serial_driver *const serial_drivers[] = {
	&mxu11x0_device, NULL
};

module_usb_serial_driver(serial_drivers, mxu1_idtable);

MODULE_AUTHOR("Mathieu Othacehe <m.othacehe@gmail.com>");
MODULE_DESCRIPTION("MOXA UPort 11x0 USB to Serial Hub Driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("moxa/moxa-1110.fw");
MODULE_FIRMWARE("moxa/moxa-1130.fw");
MODULE_FIRMWARE("moxa/moxa-1131.fw");
MODULE_FIRMWARE("moxa/moxa-1150.fw");
MODULE_FIRMWARE("moxa/moxa-1151.fw");
