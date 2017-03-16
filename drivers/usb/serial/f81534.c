/*
 * F81532/F81534 USB to Serial Ports Bridge
 *
 * F81532 => 2 Serial Ports
 * F81534 => 4 Serial Ports
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) 2016 Feature Integration Technology Inc., (Fintek)
 * Copyright (C) 2016 Tom Tsai (Tom_Tsai@fintek.com.tw)
 * Copyright (C) 2016 Peter Hong (Peter_Hong@fintek.com.tw)
 *
 * The F81532/F81534 had 1 control endpoint for setting, 1 endpoint bulk-out
 * for all serial port TX and 1 endpoint bulk-in for all serial port read in
 * (Read Data/MSR/LSR).
 *
 * Write URB is fixed with 512bytes, per serial port used 128Bytes.
 * It can be described by f81534_prepare_write_buffer()
 *
 * Read URB is 512Bytes max, per serial port used 128Bytes.
 * It can be described by f81534_process_read_urb() and maybe received with
 * 128x1,2,3,4 bytes.
 *
 */
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial_reg.h>
#include <linux/module.h>
#include <linux/uaccess.h>

/* Serial Port register Address */
#define F81534_UART_BASE_ADDRESS	0x1200
#define F81534_UART_OFFSET		0x10
#define F81534_DIVISOR_LSB_REG		(0x00 + F81534_UART_BASE_ADDRESS)
#define F81534_DIVISOR_MSB_REG		(0x01 + F81534_UART_BASE_ADDRESS)
#define F81534_FIFO_CONTROL_REG		(0x02 + F81534_UART_BASE_ADDRESS)
#define F81534_LINE_CONTROL_REG		(0x03 + F81534_UART_BASE_ADDRESS)
#define F81534_MODEM_CONTROL_REG	(0x04 + F81534_UART_BASE_ADDRESS)
#define F81534_MODEM_STATUS_REG		(0x06 + F81534_UART_BASE_ADDRESS)
#define F81534_CONFIG1_REG		(0x09 + F81534_UART_BASE_ADDRESS)

#define F81534_DEF_CONF_ADDRESS_START	0x3000
#define F81534_DEF_CONF_SIZE		8

#define F81534_CUSTOM_ADDRESS_START	0x2f00
#define F81534_CUSTOM_DATA_SIZE		0x10
#define F81534_CUSTOM_NO_CUSTOM_DATA	0xff
#define F81534_CUSTOM_VALID_TOKEN	0xf0
#define F81534_CONF_OFFSET		1

#define F81534_MAX_DATA_BLOCK		64
#define F81534_MAX_BUS_RETRY		20

/* Default URB timeout for USB operations */
#define F81534_USB_MAX_RETRY		10
#define F81534_USB_TIMEOUT		1000
#define F81534_SET_GET_REGISTER		0xA0

#define F81534_NUM_PORT			4
#define F81534_UNUSED_PORT		0xff
#define F81534_WRITE_BUFFER_SIZE	512

#define DRIVER_DESC			"Fintek F81532/F81534"
#define FINTEK_VENDOR_ID_1		0x1934
#define FINTEK_VENDOR_ID_2		0x2C42
#define FINTEK_DEVICE_ID		0x1202
#define F81534_MAX_TX_SIZE		124
#define F81534_MAX_RX_SIZE		124
#define F81534_RECEIVE_BLOCK_SIZE	128
#define F81534_MAX_RECEIVE_BLOCK_SIZE	512

#define F81534_TOKEN_RECEIVE		0x01
#define F81534_TOKEN_WRITE		0x02
#define F81534_TOKEN_TX_EMPTY		0x03
#define F81534_TOKEN_MSR_CHANGE		0x04

/*
 * We used interal SPI bus to access FLASH section. We must wait the SPI bus to
 * idle if we performed any command.
 *
 * SPI Bus status register: F81534_BUS_REG_STATUS
 *	Bit 0/1	: BUSY
 *	Bit 2	: IDLE
 */
#define F81534_BUS_BUSY			(BIT(0) | BIT(1))
#define F81534_BUS_IDLE			BIT(2)
#define F81534_BUS_READ_DATA		0x1004
#define F81534_BUS_REG_STATUS		0x1003
#define F81534_BUS_REG_START		0x1002
#define F81534_BUS_REG_END		0x1001

#define F81534_CMD_READ			0x03

#define F81534_DEFAULT_BAUD_RATE	9600
#define F81534_MAX_BAUDRATE		115200

#define F81534_PORT_CONF_DISABLE_PORT	BIT(3)
#define F81534_PORT_CONF_NOT_EXIST_PORT	BIT(7)
#define F81534_PORT_UNAVAILABLE		\
	(F81534_PORT_CONF_DISABLE_PORT | F81534_PORT_CONF_NOT_EXIST_PORT)

#define F81534_1X_RXTRIGGER		0xc3
#define F81534_8X_RXTRIGGER		0xcf

static const struct usb_device_id f81534_id_table[] = {
	{ USB_DEVICE(FINTEK_VENDOR_ID_1, FINTEK_DEVICE_ID) },
	{ USB_DEVICE(FINTEK_VENDOR_ID_2, FINTEK_DEVICE_ID) },
	{}			/* Terminating entry */
};

#define F81534_TX_EMPTY_BIT		0

struct f81534_serial_private {
	u8 conf_data[F81534_DEF_CONF_SIZE];
	int tty_idx[F81534_NUM_PORT];
	u8 setting_idx;
	int opened_port;
	struct mutex urb_mutex;
};

struct f81534_port_private {
	struct mutex mcr_mutex;
	unsigned long tx_empty;
	spinlock_t msr_lock;
	u8 shadow_mcr;
	u8 shadow_msr;
	u8 phy_num;
};

static int f81534_logic_to_phy_port(struct usb_serial *serial,
					struct usb_serial_port *port)
{
	struct f81534_serial_private *serial_priv =
			usb_get_serial_data(port->serial);
	int count = 0;
	int i;

	for (i = 0; i < F81534_NUM_PORT; ++i) {
		if (serial_priv->conf_data[i] & F81534_PORT_UNAVAILABLE)
			continue;

		if (port->port_number == count)
			return i;

		++count;
	}

	return -ENODEV;
}

static int f81534_set_register(struct usb_serial *serial, u16 reg, u8 data)
{
	struct usb_interface *interface = serial->interface;
	struct usb_device *dev = serial->dev;
	size_t count = F81534_USB_MAX_RETRY;
	int status;
	u8 *tmp;

	tmp = kmalloc(sizeof(u8), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	*tmp = data;

	/*
	 * Our device maybe not reply when heavily loading, We'll retry for
	 * F81534_USB_MAX_RETRY times.
	 */
	while (count--) {
		status = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
					 F81534_SET_GET_REGISTER,
					 USB_TYPE_VENDOR | USB_DIR_OUT,
					 reg, 0, tmp, sizeof(u8),
					 F81534_USB_TIMEOUT);
		if (status > 0) {
			status = 0;
			break;
		} else if (status == 0) {
			status = -EIO;
		}
	}

	if (status < 0) {
		dev_err(&interface->dev, "%s: reg: %x data: %x failed: %d\n",
				__func__, reg, data, status);
	}

	kfree(tmp);
	return status;
}

static int f81534_get_register(struct usb_serial *serial, u16 reg, u8 *data)
{
	struct usb_interface *interface = serial->interface;
	struct usb_device *dev = serial->dev;
	size_t count = F81534_USB_MAX_RETRY;
	int status;
	u8 *tmp;

	tmp = kmalloc(sizeof(u8), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	/*
	 * Our device maybe not reply when heavily loading, We'll retry for
	 * F81534_USB_MAX_RETRY times.
	 */
	while (count--) {
		status = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
					 F81534_SET_GET_REGISTER,
					 USB_TYPE_VENDOR | USB_DIR_IN,
					 reg, 0, tmp, sizeof(u8),
					 F81534_USB_TIMEOUT);
		if (status > 0) {
			status = 0;
			break;
		} else if (status == 0) {
			status = -EIO;
		}
	}

	if (status < 0) {
		dev_err(&interface->dev, "%s: reg: %x failed: %d\n", __func__,
				reg, status);
		goto end;
	}

	*data = *tmp;

end:
	kfree(tmp);
	return status;
}

static int f81534_set_port_register(struct usb_serial_port *port, u16 reg,
					u8 data)
{
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);

	return f81534_set_register(port->serial,
			reg + port_priv->phy_num * F81534_UART_OFFSET, data);
}

static int f81534_get_port_register(struct usb_serial_port *port, u16 reg,
					u8 *data)
{
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);

	return f81534_get_register(port->serial,
			reg + port_priv->phy_num * F81534_UART_OFFSET, data);
}

/*
 * If we try to access the internal flash via SPI bus, we should check the bus
 * status for every command. e.g., F81534_BUS_REG_START/F81534_BUS_REG_END
 */
static int f81534_wait_for_spi_idle(struct usb_serial *serial)
{
	size_t count = F81534_MAX_BUS_RETRY;
	u8 tmp;
	int status;

	do {
		status = f81534_get_register(serial, F81534_BUS_REG_STATUS,
						&tmp);
		if (status)
			return status;

		if (tmp & F81534_BUS_BUSY)
			continue;

		if (tmp & F81534_BUS_IDLE)
			break;

	} while (--count);

	if (!count) {
		dev_err(&serial->interface->dev,
				"%s: timed out waiting for idle SPI bus\n",
				__func__);
		return -EIO;
	}

	return f81534_set_register(serial, F81534_BUS_REG_STATUS,
				tmp & ~F81534_BUS_IDLE);
}

static int f81534_get_spi_register(struct usb_serial *serial, u16 reg,
					u8 *data)
{
	int status;

	status = f81534_get_register(serial, reg, data);
	if (status)
		return status;

	return f81534_wait_for_spi_idle(serial);
}

static int f81534_set_spi_register(struct usb_serial *serial, u16 reg, u8 data)
{
	int status;

	status = f81534_set_register(serial, reg, data);
	if (status)
		return status;

	return f81534_wait_for_spi_idle(serial);
}

static int f81534_read_flash(struct usb_serial *serial, u32 address,
				size_t size, u8 *buf)
{
	u8 tmp_buf[F81534_MAX_DATA_BLOCK];
	size_t block = 0;
	size_t read_size;
	size_t count;
	int status;
	int offset;
	u16 reg_tmp;

	status = f81534_set_spi_register(serial, F81534_BUS_REG_START,
					F81534_CMD_READ);
	if (status)
		return status;

	status = f81534_set_spi_register(serial, F81534_BUS_REG_START,
					(address >> 16) & 0xff);
	if (status)
		return status;

	status = f81534_set_spi_register(serial, F81534_BUS_REG_START,
					(address >> 8) & 0xff);
	if (status)
		return status;

	status = f81534_set_spi_register(serial, F81534_BUS_REG_START,
					(address >> 0) & 0xff);
	if (status)
		return status;

	/* Continuous read mode */
	do {
		read_size = min_t(size_t, F81534_MAX_DATA_BLOCK, size);

		for (count = 0; count < read_size; ++count) {
			/* To write F81534_BUS_REG_END when final byte */
			if (size <= F81534_MAX_DATA_BLOCK &&
					read_size == count + 1)
				reg_tmp = F81534_BUS_REG_END;
			else
				reg_tmp = F81534_BUS_REG_START;

			/*
			 * Dummy code, force IC to generate a read pulse, the
			 * set of value 0xf1 is dont care (any value is ok)
			 */
			status = f81534_set_spi_register(serial, reg_tmp,
					0xf1);
			if (status)
				return status;

			status = f81534_get_spi_register(serial,
						F81534_BUS_READ_DATA,
						&tmp_buf[count]);
			if (status)
				return status;

			offset = count + block * F81534_MAX_DATA_BLOCK;
			buf[offset] = tmp_buf[count];
		}

		size -= read_size;
		++block;
	} while (size);

	return 0;
}

static void f81534_prepare_write_buffer(struct usb_serial_port *port, u8 *buf)
{
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);
	int phy_num = port_priv->phy_num;
	u8 tx_len;
	int i;

	/*
	 * The block layout is fixed with 4x128 Bytes, per 128 Bytes a port.
	 * index 0: port phy idx (e.g., 0,1,2,3)
	 * index 1: only F81534_TOKEN_WRITE
	 * index 2: serial TX out length
	 * index 3: fix to 0
	 * index 4~127: serial out data block
	 */
	for (i = 0; i < F81534_NUM_PORT; ++i) {
		buf[i * F81534_RECEIVE_BLOCK_SIZE] = i;
		buf[i * F81534_RECEIVE_BLOCK_SIZE + 1] = F81534_TOKEN_WRITE;
		buf[i * F81534_RECEIVE_BLOCK_SIZE + 2] = 0;
		buf[i * F81534_RECEIVE_BLOCK_SIZE + 3] = 0;
	}

	tx_len = kfifo_out_locked(&port->write_fifo,
				&buf[phy_num * F81534_RECEIVE_BLOCK_SIZE + 4],
				F81534_MAX_TX_SIZE, &port->lock);

	buf[phy_num * F81534_RECEIVE_BLOCK_SIZE + 2] = tx_len;
}

static int f81534_submit_writer(struct usb_serial_port *port, gfp_t mem_flags)
{
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);
	struct urb *urb;
	unsigned long flags;
	int result;

	/* Check is any data in write_fifo */
	spin_lock_irqsave(&port->lock, flags);

	if (kfifo_is_empty(&port->write_fifo)) {
		spin_unlock_irqrestore(&port->lock, flags);
		return 0;
	}

	spin_unlock_irqrestore(&port->lock, flags);

	/* Check H/W is TXEMPTY */
	if (!test_and_clear_bit(F81534_TX_EMPTY_BIT, &port_priv->tx_empty))
		return 0;

	urb = port->write_urbs[0];
	f81534_prepare_write_buffer(port, port->bulk_out_buffers[0]);
	urb->transfer_buffer_length = F81534_WRITE_BUFFER_SIZE;

	result = usb_submit_urb(urb, mem_flags);
	if (result) {
		set_bit(F81534_TX_EMPTY_BIT, &port_priv->tx_empty);
		dev_err(&port->dev, "%s: submit failed: %d\n", __func__,
				result);
		return result;
	}

	usb_serial_port_softint(port);
	return 0;
}

static u32 f81534_calc_baud_divisor(u32 baudrate, u32 clockrate)
{
	if (!baudrate)
		return 0;

	/* Round to nearest divisor */
	return DIV_ROUND_CLOSEST(clockrate, baudrate);
}

static int f81534_set_port_config(struct usb_serial_port *port, u32 baudrate,
					u8 lcr)
{
	u32 divisor;
	int status;
	u8 value;

	if (baudrate <= 1200)
		value = F81534_1X_RXTRIGGER;	/* 128 FIFO & TL: 1x */
	else
		value = F81534_8X_RXTRIGGER;	/* 128 FIFO & TL: 8x */

	status = f81534_set_port_register(port, F81534_CONFIG1_REG, value);
	if (status) {
		dev_err(&port->dev, "%s: CONFIG1 setting failed\n", __func__);
		return status;
	}

	if (baudrate <= 1200)
		value = UART_FCR_TRIGGER_1 | UART_FCR_ENABLE_FIFO; /* TL: 1 */
	else
		value = UART_FCR_R_TRIG_11 | UART_FCR_ENABLE_FIFO; /* TL: 14 */

	status = f81534_set_port_register(port, F81534_FIFO_CONTROL_REG,
						value);
	if (status) {
		dev_err(&port->dev, "%s: FCR setting failed\n", __func__);
		return status;
	}

	divisor = f81534_calc_baud_divisor(baudrate, F81534_MAX_BAUDRATE);
	value = UART_LCR_DLAB;
	status = f81534_set_port_register(port, F81534_LINE_CONTROL_REG,
						value);
	if (status) {
		dev_err(&port->dev, "%s: set LCR failed\n", __func__);
		return status;
	}

	value = divisor & 0xff;
	status = f81534_set_port_register(port, F81534_DIVISOR_LSB_REG, value);
	if (status) {
		dev_err(&port->dev, "%s: set DLAB LSB failed\n", __func__);
		return status;
	}

	value = (divisor >> 8) & 0xff;
	status = f81534_set_port_register(port, F81534_DIVISOR_MSB_REG, value);
	if (status) {
		dev_err(&port->dev, "%s: set DLAB MSB failed\n", __func__);
		return status;
	}

	status = f81534_set_port_register(port, F81534_LINE_CONTROL_REG, lcr);
	if (status) {
		dev_err(&port->dev, "%s: set LCR failed\n", __func__);
		return status;
	}

	return 0;
}

static int f81534_update_mctrl(struct usb_serial_port *port, unsigned int set,
				unsigned int clear)
{
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);
	int status;
	u8 tmp;

	if (((set | clear) & (TIOCM_DTR | TIOCM_RTS)) == 0)
		return 0;	/* no change */

	mutex_lock(&port_priv->mcr_mutex);

	/* 'Set' takes precedence over 'Clear' */
	clear &= ~set;

	/* Always enable UART_MCR_OUT2 */
	tmp = UART_MCR_OUT2 | port_priv->shadow_mcr;

	if (clear & TIOCM_DTR)
		tmp &= ~UART_MCR_DTR;

	if (clear & TIOCM_RTS)
		tmp &= ~UART_MCR_RTS;

	if (set & TIOCM_DTR)
		tmp |= UART_MCR_DTR;

	if (set & TIOCM_RTS)
		tmp |= UART_MCR_RTS;

	status = f81534_set_port_register(port, F81534_MODEM_CONTROL_REG, tmp);
	if (status < 0) {
		dev_err(&port->dev, "%s: MCR write failed\n", __func__);
		mutex_unlock(&port_priv->mcr_mutex);
		return status;
	}

	port_priv->shadow_mcr = tmp;
	mutex_unlock(&port_priv->mcr_mutex);
	return 0;
}

/*
 * This function will search the data area with token F81534_CUSTOM_VALID_TOKEN
 * for latest configuration index. If nothing found
 * (*index = F81534_CUSTOM_NO_CUSTOM_DATA), We'll load default configure in
 * F81534_DEF_CONF_ADDRESS_START section.
 *
 * Due to we only use block0 to save data, so *index should be 0 or
 * F81534_CUSTOM_NO_CUSTOM_DATA.
 */
static int f81534_find_config_idx(struct usb_serial *serial, u8 *index)
{
	u8 tmp;
	int status;

	status = f81534_read_flash(serial, F81534_CUSTOM_ADDRESS_START, 1,
					&tmp);
	if (status) {
		dev_err(&serial->interface->dev, "%s: read failed: %d\n",
				__func__, status);
		return status;
	}

	/* We'll use the custom data when the data is valid. */
	if (tmp == F81534_CUSTOM_VALID_TOKEN)
		*index = 0;
	else
		*index = F81534_CUSTOM_NO_CUSTOM_DATA;

	return 0;
}

/*
 * We had 2 generation of F81532/534 IC. All has an internal storage.
 *
 * 1st is pure USB-to-TTL RS232 IC and designed for 4 ports only, no any
 * internal data will used. All mode and gpio control should manually set
 * by AP or Driver and all storage space value are 0xff. The
 * f81534_calc_num_ports() will run to final we marked as "oldest version"
 * for this IC.
 *
 * 2rd is designed to more generic to use any transceiver and this is our
 * mass production type. We'll save data in F81534_CUSTOM_ADDRESS_START
 * (0x2f00) with 9bytes. The 1st byte is a indicater. If the token is
 * F81534_CUSTOM_VALID_TOKEN(0xf0), the IC is 2nd gen type, the following
 * 4bytes save port mode (0:RS232/1:RS485 Invert/2:RS485), and the last
 * 4bytes save GPIO state(value from 0~7 to represent 3 GPIO output pin).
 * The f81534_calc_num_ports() will run to "new style" with checking
 * F81534_PORT_UNAVAILABLE section.
 */
static int f81534_calc_num_ports(struct usb_serial *serial,
					struct usb_serial_endpoints *epds)
{
	struct device *dev = &serial->interface->dev;
	int size_bulk_in = usb_endpoint_maxp(epds->bulk_in[0]);
	int size_bulk_out = usb_endpoint_maxp(epds->bulk_out[0]);
	u8 setting[F81534_CUSTOM_DATA_SIZE];
	u8 setting_idx;
	u8 num_port = 0;
	int status;
	size_t i;

	if (size_bulk_out != F81534_WRITE_BUFFER_SIZE ||
			size_bulk_in != F81534_MAX_RECEIVE_BLOCK_SIZE) {
		dev_err(dev, "unsupported endpoint max packet size\n");
		return -ENODEV;
	}

	/* Check had custom setting */
	status = f81534_find_config_idx(serial, &setting_idx);
	if (status) {
		dev_err(&serial->interface->dev, "%s: find idx failed: %d\n",
				__func__, status);
		return status;
	}

	/*
	 * We'll read custom data only when data available, otherwise we'll
	 * read default value instead.
	 */
	if (setting_idx != F81534_CUSTOM_NO_CUSTOM_DATA) {
		status = f81534_read_flash(serial,
						F81534_CUSTOM_ADDRESS_START +
						F81534_CONF_OFFSET,
						sizeof(setting), setting);
		if (status) {
			dev_err(&serial->interface->dev,
					"%s: get custom data failed: %d\n",
					__func__, status);
			return status;
		}

		dev_dbg(&serial->interface->dev,
				"%s: read config from block: %d\n", __func__,
				setting_idx);
	} else {
		/* Read default board setting */
		status = f81534_read_flash(serial,
				F81534_DEF_CONF_ADDRESS_START, F81534_NUM_PORT,
				setting);

		if (status) {
			dev_err(&serial->interface->dev,
					"%s: read failed: %d\n", __func__,
					status);
			return status;
		}

		dev_dbg(&serial->interface->dev, "%s: read default config\n",
				__func__);
	}

	/* New style, find all possible ports */
	for (i = 0; i < F81534_NUM_PORT; ++i) {
		if (setting[i] & F81534_PORT_UNAVAILABLE)
			continue;

		++num_port;
	}

	if (!num_port) {
		dev_warn(&serial->interface->dev,
			"no config found, assuming 4 ports\n");
		num_port = 4;		/* Nothing found, oldest version IC */
	}

	/*
	 * Setup bulk-out endpoint multiplexing. All ports share the same
	 * bulk-out endpoint.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(epds->bulk_out) < F81534_NUM_PORT);

	for (i = 1; i < num_port; ++i)
		epds->bulk_out[i] = epds->bulk_out[0];

	epds->num_bulk_out = num_port;

	return num_port;
}

static void f81534_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				struct ktermios *old_termios)
{
	u8 new_lcr = 0;
	int status;
	u32 baud;

	if (C_BAUD(tty) == B0)
		f81534_update_mctrl(port, 0, TIOCM_DTR | TIOCM_RTS);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		f81534_update_mctrl(port, TIOCM_DTR | TIOCM_RTS, 0);

	if (C_PARENB(tty)) {
		new_lcr |= UART_LCR_PARITY;

		if (!C_PARODD(tty))
			new_lcr |= UART_LCR_EPAR;

		if (C_CMSPAR(tty))
			new_lcr |= UART_LCR_SPAR;
	}

	if (C_CSTOPB(tty))
		new_lcr |= UART_LCR_STOP;

	switch (C_CSIZE(tty)) {
	case CS5:
		new_lcr |= UART_LCR_WLEN5;
		break;
	case CS6:
		new_lcr |= UART_LCR_WLEN6;
		break;
	case CS7:
		new_lcr |= UART_LCR_WLEN7;
		break;
	default:
	case CS8:
		new_lcr |= UART_LCR_WLEN8;
		break;
	}

	baud = tty_get_baud_rate(tty);
	if (!baud)
		return;

	if (baud > F81534_MAX_BAUDRATE) {
		if (old_termios)
			baud = tty_termios_baud_rate(old_termios);
		else
			baud = F81534_DEFAULT_BAUD_RATE;

		tty_encode_baud_rate(tty, baud, baud);
	}

	dev_dbg(&port->dev, "%s: baud: %d\n", __func__, baud);

	status = f81534_set_port_config(port, baud, new_lcr);
	if (status < 0) {
		dev_err(&port->dev, "%s: set port config failed: %d\n",
				__func__, status);
	}
}

static int f81534_submit_read_urb(struct usb_serial *serial, gfp_t flags)
{
	return usb_serial_generic_submit_read_urbs(serial->port[0], flags);
}

static void f81534_msr_changed(struct usb_serial_port *port, u8 msr)
{
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);
	struct tty_struct *tty;
	unsigned long flags;
	u8 old_msr;

	if (!(msr & UART_MSR_ANY_DELTA))
		return;

	spin_lock_irqsave(&port_priv->msr_lock, flags);
	old_msr = port_priv->shadow_msr;
	port_priv->shadow_msr = msr;
	spin_unlock_irqrestore(&port_priv->msr_lock, flags);

	dev_dbg(&port->dev, "%s: MSR from %02x to %02x\n", __func__, old_msr,
			msr);

	/* Update input line counters */
	if (msr & UART_MSR_DCTS)
		port->icount.cts++;
	if (msr & UART_MSR_DDSR)
		port->icount.dsr++;
	if (msr & UART_MSR_DDCD)
		port->icount.dcd++;
	if (msr & UART_MSR_TERI)
		port->icount.rng++;

	wake_up_interruptible(&port->port.delta_msr_wait);

	if (!(msr & UART_MSR_DDCD))
		return;

	dev_dbg(&port->dev, "%s: DCD Changed: phy_num: %d from %x to %x\n",
			__func__, port_priv->phy_num, old_msr, msr);

	tty = tty_port_tty_get(&port->port);
	if (!tty)
		return;

	usb_serial_handle_dcd_change(port, tty, msr & UART_MSR_DCD);
	tty_kref_put(tty);
}

static int f81534_read_msr(struct usb_serial_port *port)
{
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int status;
	u8 msr;

	/* Get MSR initial value */
	status = f81534_get_port_register(port, F81534_MODEM_STATUS_REG, &msr);
	if (status)
		return status;

	/* Force update current state */
	spin_lock_irqsave(&port_priv->msr_lock, flags);
	port_priv->shadow_msr = msr;
	spin_unlock_irqrestore(&port_priv->msr_lock, flags);

	return 0;
}

static int f81534_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct f81534_serial_private *serial_priv =
			usb_get_serial_data(port->serial);
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);
	int status;

	status = f81534_set_port_register(port,
				F81534_FIFO_CONTROL_REG, UART_FCR_ENABLE_FIFO |
				UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
	if (status) {
		dev_err(&port->dev, "%s: Clear FIFO failed: %d\n", __func__,
				status);
		return status;
	}

	if (tty)
		f81534_set_termios(tty, port, NULL);

	status = f81534_read_msr(port);
	if (status)
		return status;

	mutex_lock(&serial_priv->urb_mutex);

	/* Submit Read URBs for first port opened */
	if (!serial_priv->opened_port) {
		status = f81534_submit_read_urb(port->serial, GFP_KERNEL);
		if (status)
			goto exit;
	}

	serial_priv->opened_port++;

exit:
	mutex_unlock(&serial_priv->urb_mutex);

	set_bit(F81534_TX_EMPTY_BIT, &port_priv->tx_empty);
	return status;
}

static void f81534_close(struct usb_serial_port *port)
{
	struct f81534_serial_private *serial_priv =
			usb_get_serial_data(port->serial);
	struct usb_serial_port *port0 = port->serial->port[0];
	unsigned long flags;
	size_t i;

	usb_kill_urb(port->write_urbs[0]);

	spin_lock_irqsave(&port->lock, flags);
	kfifo_reset_out(&port->write_fifo);
	spin_unlock_irqrestore(&port->lock, flags);

	/* Kill Read URBs when final port closed */
	mutex_lock(&serial_priv->urb_mutex);
	serial_priv->opened_port--;

	if (!serial_priv->opened_port) {
		for (i = 0; i < ARRAY_SIZE(port0->read_urbs); ++i)
			usb_kill_urb(port0->read_urbs[i]);
	}

	mutex_unlock(&serial_priv->urb_mutex);
}

static int f81534_get_serial_info(struct usb_serial_port *port,
				  struct serial_struct __user *retinfo)
{
	struct f81534_port_private *port_priv;
	struct serial_struct tmp;

	port_priv = usb_get_serial_port_data(port);

	memset(&tmp, 0, sizeof(tmp));

	tmp.type = PORT_16550A;
	tmp.port = port->port_number;
	tmp.line = port->minor;
	tmp.baud_base = F81534_MAX_BAUDRATE;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}

static int f81534_ioctl(struct tty_struct *tty, unsigned int cmd,
			unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct serial_struct __user *buf = (struct serial_struct __user *)arg;

	switch (cmd) {
	case TIOCGSERIAL:
		return f81534_get_serial_info(port, buf);
	default:
		break;
	}

	return -ENOIOCTLCMD;
}

static void f81534_process_per_serial_block(struct usb_serial_port *port,
		u8 *data)
{
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);
	int phy_num = data[0];
	size_t read_size = 0;
	size_t i;
	char tty_flag;
	int status;
	u8 lsr;

	/*
	 * The block layout is 128 Bytes
	 * index 0: port phy idx (e.g., 0,1,2,3),
	 * index 1: It's could be
	 *			F81534_TOKEN_RECEIVE
	 *			F81534_TOKEN_TX_EMPTY
	 *			F81534_TOKEN_MSR_CHANGE
	 * index 2: serial in size (data+lsr, must be even)
	 *			meaningful for F81534_TOKEN_RECEIVE only
	 * index 3: current MSR with this device
	 * index 4~127: serial in data block (data+lsr, must be even)
	 */
	switch (data[1]) {
	case F81534_TOKEN_TX_EMPTY:
		set_bit(F81534_TX_EMPTY_BIT, &port_priv->tx_empty);

		/* Try to submit writer */
		status = f81534_submit_writer(port, GFP_ATOMIC);
		if (status)
			dev_err(&port->dev, "%s: submit failed\n", __func__);
		return;

	case F81534_TOKEN_MSR_CHANGE:
		f81534_msr_changed(port, data[3]);
		return;

	case F81534_TOKEN_RECEIVE:
		read_size = data[2];
		if (read_size > F81534_MAX_RX_SIZE) {
			dev_err(&port->dev,
				"%s: phy: %d read_size: %zu larger than: %d\n",
				__func__, phy_num, read_size,
				F81534_MAX_RX_SIZE);
			return;
		}

		break;

	default:
		dev_warn(&port->dev, "%s: unknown token: %02x\n", __func__,
				data[1]);
		return;
	}

	for (i = 4; i < 4 + read_size; i += 2) {
		tty_flag = TTY_NORMAL;
		lsr = data[i + 1];

		if (lsr & UART_LSR_BRK_ERROR_BITS) {
			if (lsr & UART_LSR_BI) {
				tty_flag = TTY_BREAK;
				port->icount.brk++;
				usb_serial_handle_break(port);
			} else if (lsr & UART_LSR_PE) {
				tty_flag = TTY_PARITY;
				port->icount.parity++;
			} else if (lsr & UART_LSR_FE) {
				tty_flag = TTY_FRAME;
				port->icount.frame++;
			}

			if (lsr & UART_LSR_OE) {
				port->icount.overrun++;
				tty_insert_flip_char(&port->port, 0,
						TTY_OVERRUN);
			}
		}

		if (port->port.console && port->sysrq) {
			if (usb_serial_handle_sysrq_char(port, data[i]))
				continue;
		}

		tty_insert_flip_char(&port->port, data[i], tty_flag);
	}

	tty_flip_buffer_push(&port->port);
}

static void f81534_process_read_urb(struct urb *urb)
{
	struct f81534_serial_private *serial_priv;
	struct usb_serial_port *port;
	struct usb_serial *serial;
	u8 *buf;
	int phy_port_num;
	int tty_port_num;
	size_t i;

	if (!urb->actual_length ||
			urb->actual_length % F81534_RECEIVE_BLOCK_SIZE) {
		return;
	}

	port = urb->context;
	serial = port->serial;
	buf = urb->transfer_buffer;
	serial_priv = usb_get_serial_data(serial);

	for (i = 0; i < urb->actual_length; i += F81534_RECEIVE_BLOCK_SIZE) {
		phy_port_num = buf[i];
		if (phy_port_num >= F81534_NUM_PORT) {
			dev_err(&port->dev,
				"%s: phy_port_num: %d larger than: %d\n",
				__func__, phy_port_num, F81534_NUM_PORT);
			continue;
		}

		tty_port_num = serial_priv->tty_idx[phy_port_num];
		port = serial->port[tty_port_num];

		if (tty_port_initialized(&port->port))
			f81534_process_per_serial_block(port, &buf[i]);
	}
}

static void f81534_write_usb_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&port->dev, "%s - urb stopped: %d\n",
				__func__, urb->status);
		return;
	case -EPIPE:
		dev_err(&port->dev, "%s - urb stopped: %d\n",
				__func__, urb->status);
		return;
	default:
		dev_dbg(&port->dev, "%s - nonzero urb status: %d\n",
				__func__, urb->status);
		break;
	}
}

static int f81534_attach(struct usb_serial *serial)
{
	struct f81534_serial_private *serial_priv;
	int index = 0;
	int status;
	int i;

	serial_priv = devm_kzalloc(&serial->interface->dev,
					sizeof(*serial_priv), GFP_KERNEL);
	if (!serial_priv)
		return -ENOMEM;

	usb_set_serial_data(serial, serial_priv);

	mutex_init(&serial_priv->urb_mutex);

	/* Check had custom setting */
	status = f81534_find_config_idx(serial, &serial_priv->setting_idx);
	if (status) {
		dev_err(&serial->interface->dev, "%s: find idx failed: %d\n",
				__func__, status);
		return status;
	}

	/*
	 * We'll read custom data only when data available, otherwise we'll
	 * read default value instead.
	 */
	if (serial_priv->setting_idx == F81534_CUSTOM_NO_CUSTOM_DATA) {
		/*
		 * The default configuration layout:
		 *	byte 0/1/2/3: uart setting
		 */
		status = f81534_read_flash(serial,
					F81534_DEF_CONF_ADDRESS_START,
					F81534_DEF_CONF_SIZE,
					serial_priv->conf_data);
		if (status) {
			dev_err(&serial->interface->dev,
					"%s: read reserve data failed: %d\n",
					__func__, status);
			return status;
		}
	} else {
		/* Only read 8 bytes for mode & GPIO */
		status = f81534_read_flash(serial,
						F81534_CUSTOM_ADDRESS_START +
						F81534_CONF_OFFSET,
						sizeof(serial_priv->conf_data),
						serial_priv->conf_data);
		if (status) {
			dev_err(&serial->interface->dev,
					"%s: idx: %d get data failed: %d\n",
					__func__, serial_priv->setting_idx,
					status);
			return status;
		}
	}

	/* Assign phy-to-logic mapping */
	for (i = 0; i < F81534_NUM_PORT; ++i) {
		if (serial_priv->conf_data[i] & F81534_PORT_UNAVAILABLE)
			continue;

		serial_priv->tty_idx[i] = index++;
		dev_dbg(&serial->interface->dev,
				"%s: phy_num: %d, tty_idx: %d\n", __func__, i,
				serial_priv->tty_idx[i]);
	}

	return 0;
}

static int f81534_port_probe(struct usb_serial_port *port)
{
	struct f81534_port_private *port_priv;
	int ret;

	port_priv = devm_kzalloc(&port->dev, sizeof(*port_priv), GFP_KERNEL);
	if (!port_priv)
		return -ENOMEM;

	spin_lock_init(&port_priv->msr_lock);
	mutex_init(&port_priv->mcr_mutex);

	/* Assign logic-to-phy mapping */
	ret = f81534_logic_to_phy_port(port->serial, port);
	if (ret < 0)
		return ret;

	port_priv->phy_num = ret;
	usb_set_serial_port_data(port, port_priv);
	dev_dbg(&port->dev, "%s: port_number: %d, phy_num: %d\n", __func__,
			port->port_number, port_priv->phy_num);

	return 0;
}

static int f81534_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);
	int status;
	int r;
	u8 msr;
	u8 mcr;

	/* Read current MSR from device */
	status = f81534_get_port_register(port, F81534_MODEM_STATUS_REG, &msr);
	if (status)
		return status;

	mutex_lock(&port_priv->mcr_mutex);
	mcr = port_priv->shadow_mcr;
	mutex_unlock(&port_priv->mcr_mutex);

	r = (mcr & UART_MCR_DTR ? TIOCM_DTR : 0) |
	    (mcr & UART_MCR_RTS ? TIOCM_RTS : 0) |
	    (msr & UART_MSR_CTS ? TIOCM_CTS : 0) |
	    (msr & UART_MSR_DCD ? TIOCM_CAR : 0) |
	    (msr & UART_MSR_RI ? TIOCM_RI : 0) |
	    (msr & UART_MSR_DSR ? TIOCM_DSR : 0);

	return r;
}

static int f81534_tiocmset(struct tty_struct *tty, unsigned int set,
				unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;

	return f81534_update_mctrl(port, set, clear);
}

static void f81534_dtr_rts(struct usb_serial_port *port, int on)
{
	if (on)
		f81534_update_mctrl(port, TIOCM_DTR | TIOCM_RTS, 0);
	else
		f81534_update_mctrl(port, 0, TIOCM_DTR | TIOCM_RTS);
}

static int f81534_write(struct tty_struct *tty, struct usb_serial_port *port,
			const u8 *buf, int count)
{
	int bytes_out, status;

	if (!count)
		return 0;

	bytes_out = kfifo_in_locked(&port->write_fifo, buf, count,
					&port->lock);

	status = f81534_submit_writer(port, GFP_ATOMIC);
	if (status) {
		dev_err(&port->dev, "%s: submit failed\n", __func__);
		return status;
	}

	return bytes_out;
}

static bool f81534_tx_empty(struct usb_serial_port *port)
{
	struct f81534_port_private *port_priv = usb_get_serial_port_data(port);

	return test_bit(F81534_TX_EMPTY_BIT, &port_priv->tx_empty);
}

static int f81534_resume(struct usb_serial *serial)
{
	struct f81534_serial_private *serial_priv =
			usb_get_serial_data(serial);
	struct usb_serial_port *port;
	int error = 0;
	int status;
	size_t i;

	/*
	 * We'll register port 0 bulkin when port had opened, It'll take all
	 * port received data, MSR register change and TX_EMPTY information.
	 */
	mutex_lock(&serial_priv->urb_mutex);

	if (serial_priv->opened_port) {
		status = f81534_submit_read_urb(serial, GFP_NOIO);
		if (status) {
			mutex_unlock(&serial_priv->urb_mutex);
			return status;
		}
	}

	mutex_unlock(&serial_priv->urb_mutex);

	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		if (!tty_port_initialized(&port->port))
			continue;

		status = f81534_submit_writer(port, GFP_NOIO);
		if (status) {
			dev_err(&port->dev, "%s: submit failed\n", __func__);
			++error;
		}
	}

	if (error)
		return -EIO;

	return 0;
}

static struct usb_serial_driver f81534_device = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "f81534",
	},
	.description =		DRIVER_DESC,
	.id_table =		f81534_id_table,
	.num_bulk_in =		1,
	.num_bulk_out =		1,
	.open =			f81534_open,
	.close =		f81534_close,
	.write =		f81534_write,
	.tx_empty =		f81534_tx_empty,
	.calc_num_ports =	f81534_calc_num_ports,
	.attach =		f81534_attach,
	.port_probe =		f81534_port_probe,
	.dtr_rts =		f81534_dtr_rts,
	.process_read_urb =	f81534_process_read_urb,
	.ioctl =		f81534_ioctl,
	.tiocmget =		f81534_tiocmget,
	.tiocmset =		f81534_tiocmset,
	.write_bulk_callback =	f81534_write_usb_callback,
	.set_termios =		f81534_set_termios,
	.resume =		f81534_resume,
};

static struct usb_serial_driver *const serial_drivers[] = {
	&f81534_device, NULL
};

module_usb_serial_driver(serial_drivers, f81534_id_table);

MODULE_DEVICE_TABLE(usb, f81534_id_table);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Peter Hong <Peter_Hong@fintek.com.tw>");
MODULE_AUTHOR("Tom Tsai <Tom_Tsai@fintek.com.tw>");
MODULE_LICENSE("GPL");
