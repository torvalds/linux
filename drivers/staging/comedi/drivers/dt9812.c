/*
 * comedi/drivers/dt9812.c
 *   COMEDI driver for DataTranslation DT9812 USB module
 *
 * Copyright (C) 2005 Anders Blomdell <anders.blomdell@control.lth.se>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
Driver: dt9812
Description: Data Translation DT9812 USB module
Author: anders.blomdell@control.lth.se (Anders Blomdell)
Status: in development
Devices: [Data Translation] DT9812 (dt9812)
Updated: Sun Nov 20 20:18:34 EST 2005

This driver works, but bulk transfers not implemented. Might be a starting point
for someone else. I found out too late that USB has too high latencies (>1 ms)
for my needs.
*/

/*
 * Nota Bene:
 *   1. All writes to command pipe has to be 32 bytes (ISP1181B SHRTP=0 ?)
 *   2. The DDK source (as of sep 2005) is in error regarding the
 *      input MUX bits (example code says P4, but firmware schematics
 *      says P1).
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#include "../comedidev.h"

#define DT9812_DIAGS_BOARD_INFO_ADDR	0xFBFF
#define DT9812_MAX_WRITE_CMD_PIPE_SIZE	32
#define DT9812_MAX_READ_CMD_PIPE_SIZE	32

/* usb_bulk_msg() timout in milliseconds */
#define DT9812_USB_TIMEOUT		1000

/*
 * See Silican Laboratories C8051F020/1/2/3 manual
 */
#define F020_SFR_P4			0x84
#define F020_SFR_P1			0x90
#define F020_SFR_P2			0xa0
#define F020_SFR_P3			0xb0
#define F020_SFR_AMX0CF			0xba
#define F020_SFR_AMX0SL			0xbb
#define F020_SFR_ADC0CF			0xbc
#define F020_SFR_ADC0L			0xbe
#define F020_SFR_ADC0H			0xbf
#define F020_SFR_DAC0L			0xd2
#define F020_SFR_DAC0H			0xd3
#define F020_SFR_DAC0CN			0xd4
#define F020_SFR_DAC1L			0xd5
#define F020_SFR_DAC1H			0xd6
#define F020_SFR_DAC1CN			0xd7
#define F020_SFR_ADC0CN			0xe8

#define F020_MASK_ADC0CF_AMP0GN0	0x01
#define F020_MASK_ADC0CF_AMP0GN1	0x02
#define F020_MASK_ADC0CF_AMP0GN2	0x04

#define F020_MASK_ADC0CN_AD0EN		0x80
#define F020_MASK_ADC0CN_AD0INT		0x20
#define F020_MASK_ADC0CN_AD0BUSY	0x10

#define F020_MASK_DACxCN_DACxEN		0x80

enum {
					/* A/D  D/A  DI  DO  CT */
	DT9812_DEVID_DT9812_10,		/*  8    2   8   8   1  +/- 10V */
	DT9812_DEVID_DT9812_2PT5,	/*  8    2   8   8   1  0-2.44V */
};

enum dt9812_gain {
	DT9812_GAIN_0PT25 = 1,
	DT9812_GAIN_0PT5 = 2,
	DT9812_GAIN_1 = 4,
	DT9812_GAIN_2 = 8,
	DT9812_GAIN_4 = 16,
	DT9812_GAIN_8 = 32,
	DT9812_GAIN_16 = 64,
};

enum {
	DT9812_LEAST_USB_FIRMWARE_CMD_CODE = 0,
	/* Write Flash memory */
	DT9812_W_FLASH_DATA = 0,
	/* Read Flash memory misc config info */
	DT9812_R_FLASH_DATA = 1,

	/*
	 * Register read/write commands for processor
	 */

	/* Read a single byte of USB memory */
	DT9812_R_SINGLE_BYTE_REG = 2,
	/* Write a single byte of USB memory */
	DT9812_W_SINGLE_BYTE_REG = 3,
	/* Multiple Reads of USB memory */
	DT9812_R_MULTI_BYTE_REG = 4,
	/* Multiple Writes of USB memory */
	DT9812_W_MULTI_BYTE_REG = 5,
	/* Read, (AND) with mask, OR value, then write (single) */
	DT9812_RMW_SINGLE_BYTE_REG = 6,
	/* Read, (AND) with mask, OR value, then write (multiple) */
	DT9812_RMW_MULTI_BYTE_REG = 7,

	/*
	 * Register read/write commands for SMBus
	 */

	/* Read a single byte of SMBus */
	DT9812_R_SINGLE_BYTE_SMBUS = 8,
	/* Write a single byte of SMBus */
	DT9812_W_SINGLE_BYTE_SMBUS = 9,
	/* Multiple Reads of SMBus */
	DT9812_R_MULTI_BYTE_SMBUS = 10,
	/* Multiple Writes of SMBus */
	DT9812_W_MULTI_BYTE_SMBUS = 11,

	/*
	 * Register read/write commands for a device
	 */

	/* Read a single byte of a device */
	DT9812_R_SINGLE_BYTE_DEV = 12,
	/* Write a single byte of a device */
	DT9812_W_SINGLE_BYTE_DEV = 13,
	/* Multiple Reads of a device */
	DT9812_R_MULTI_BYTE_DEV = 14,
	/* Multiple Writes of a device */
	DT9812_W_MULTI_BYTE_DEV = 15,

	/* Not sure if we'll need this */
	DT9812_W_DAC_THRESHOLD = 16,

	/* Set interrupt on change mask */
	DT9812_W_INT_ON_CHANGE_MASK = 17,

	/* Write (or Clear) the CGL for the ADC */
	DT9812_W_CGL = 18,
	/* Multiple Reads of USB memory */
	DT9812_R_MULTI_BYTE_USBMEM = 19,
	/* Multiple Writes to USB memory */
	DT9812_W_MULTI_BYTE_USBMEM = 20,

	/* Issue a start command to a given subsystem */
	DT9812_START_SUBSYSTEM = 21,
	/* Issue a stop command to a given subsystem */
	DT9812_STOP_SUBSYSTEM = 22,

	/* calibrate the board using CAL_POT_CMD */
	DT9812_CALIBRATE_POT = 23,
	/* set the DAC FIFO size */
	DT9812_W_DAC_FIFO_SIZE = 24,
	/* Write or Clear the CGL for the DAC */
	DT9812_W_CGL_DAC = 25,
	/* Read a single value from a subsystem */
	DT9812_R_SINGLE_VALUE_CMD = 26,
	/* Write a single value to a subsystem */
	DT9812_W_SINGLE_VALUE_CMD = 27,
	/* Valid DT9812_USB_FIRMWARE_CMD_CODE's will be less than this number */
	DT9812_MAX_USB_FIRMWARE_CMD_CODE,
};

struct dt9812_flash_data {
	__le16 numbytes;
	__le16 address;
};

#define DT9812_MAX_NUM_MULTI_BYTE_RDS  \
	((DT9812_MAX_WRITE_CMD_PIPE_SIZE - 4 - 1) / sizeof(u8))

struct dt9812_read_multi {
	u8 count;
	u8 address[DT9812_MAX_NUM_MULTI_BYTE_RDS];
};

struct dt9812_write_byte {
	u8 address;
	u8 value;
};

#define DT9812_MAX_NUM_MULTI_BYTE_WRTS  \
	((DT9812_MAX_WRITE_CMD_PIPE_SIZE - 4 - 1) / \
	 sizeof(struct dt9812_write_byte))

struct dt9812_write_multi {
	u8 count;
	struct dt9812_write_byte write[DT9812_MAX_NUM_MULTI_BYTE_WRTS];
};

struct dt9812_rmw_byte {
	u8 address;
	u8 and_mask;
	u8 or_value;
};

#define DT9812_MAX_NUM_MULTI_BYTE_RMWS  \
	((DT9812_MAX_WRITE_CMD_PIPE_SIZE - 4 - 1) / \
	 sizeof(struct dt9812_rmw_byte))

struct dt9812_rmw_multi {
	u8 count;
	struct dt9812_rmw_byte rmw[DT9812_MAX_NUM_MULTI_BYTE_RMWS];
};

struct dt9812_usb_cmd {
	__le32 cmd;
	union {
		struct dt9812_flash_data flash_data_info;
		struct dt9812_read_multi read_multi_info;
		struct dt9812_write_multi write_multi_info;
		struct dt9812_rmw_multi rmw_multi_info;
	} u;
};

struct dt9812_private {
	struct semaphore sem;
	struct {
		__u8 addr;
		size_t size;
	} cmd_wr, cmd_rd;
	u16 device;
};

static int dt9812_read_info(struct comedi_device *dev,
			    int offset, void *buf, size_t buf_size)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct dt9812_private *devpriv = dev->private;
	struct dt9812_usb_cmd cmd;
	int count, ret;

	cmd.cmd = cpu_to_le32(DT9812_R_FLASH_DATA);
	cmd.u.flash_data_info.address =
	    cpu_to_le16(DT9812_DIAGS_BOARD_INFO_ADDR + offset);
	cmd.u.flash_data_info.numbytes = cpu_to_le16(buf_size);

	/* DT9812 only responds to 32 byte writes!! */
	ret = usb_bulk_msg(usb, usb_sndbulkpipe(usb, devpriv->cmd_wr.addr),
			   &cmd, 32, &count, DT9812_USB_TIMEOUT);
	if (ret)
		return ret;

	return usb_bulk_msg(usb, usb_rcvbulkpipe(usb, devpriv->cmd_rd.addr),
			    buf, buf_size, &count, DT9812_USB_TIMEOUT);
}

static int dt9812_read_multiple_registers(struct comedi_device *dev,
					  int reg_count, u8 *address,
					  u8 *value)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct dt9812_private *devpriv = dev->private;
	struct dt9812_usb_cmd cmd;
	int i, count, ret;

	cmd.cmd = cpu_to_le32(DT9812_R_MULTI_BYTE_REG);
	cmd.u.read_multi_info.count = reg_count;
	for (i = 0; i < reg_count; i++)
		cmd.u.read_multi_info.address[i] = address[i];

	/* DT9812 only responds to 32 byte writes!! */
	ret = usb_bulk_msg(usb, usb_sndbulkpipe(usb, devpriv->cmd_wr.addr),
			   &cmd, 32, &count, DT9812_USB_TIMEOUT);
	if (ret)
		return ret;

	return usb_bulk_msg(usb, usb_rcvbulkpipe(usb, devpriv->cmd_rd.addr),
			    value, reg_count, &count, DT9812_USB_TIMEOUT);
}

static int dt9812_write_multiple_registers(struct comedi_device *dev,
					   int reg_count, u8 *address,
					   u8 *value)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct dt9812_private *devpriv = dev->private;
	struct dt9812_usb_cmd cmd;
	int i, count;

	cmd.cmd = cpu_to_le32(DT9812_W_MULTI_BYTE_REG);
	cmd.u.read_multi_info.count = reg_count;
	for (i = 0; i < reg_count; i++) {
		cmd.u.write_multi_info.write[i].address = address[i];
		cmd.u.write_multi_info.write[i].value = value[i];
	}

	/* DT9812 only responds to 32 byte writes!! */
	return usb_bulk_msg(usb, usb_sndbulkpipe(usb, devpriv->cmd_wr.addr),
			    &cmd, 32, &count, DT9812_USB_TIMEOUT);
}

static int dt9812_rmw_multiple_registers(struct comedi_device *dev,
					 int reg_count,
					 struct dt9812_rmw_byte *rmw)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct dt9812_private *devpriv = dev->private;
	struct dt9812_usb_cmd cmd;
	int i, count;

	cmd.cmd = cpu_to_le32(DT9812_RMW_MULTI_BYTE_REG);
	cmd.u.rmw_multi_info.count = reg_count;
	for (i = 0; i < reg_count; i++)
		cmd.u.rmw_multi_info.rmw[i] = rmw[i];

	/* DT9812 only responds to 32 byte writes!! */
	return usb_bulk_msg(usb, usb_sndbulkpipe(usb, devpriv->cmd_wr.addr),
			    &cmd, 32, &count, DT9812_USB_TIMEOUT);
}

static int dt9812_digital_in(struct comedi_device *dev, u8 *bits)
{
	struct dt9812_private *devpriv = dev->private;
	u8 reg[2] = { F020_SFR_P3, F020_SFR_P1 };
	u8 value[2];
	int ret;

	down(&devpriv->sem);
	ret = dt9812_read_multiple_registers(dev, 2, reg, value);
	if (ret == 0) {
		/*
		 * bits 0-6 in F020_SFR_P3 are bits 0-6 in the digital
		 * input port bit 3 in F020_SFR_P1 is bit 7 in the
		 * digital input port
		 */
		*bits = (value[0] & 0x7f) | ((value[1] & 0x08) << 4);
	}
	up(&devpriv->sem);

	return ret;
}

static int dt9812_digital_out(struct comedi_device *dev, u8 bits)
{
	struct dt9812_private *devpriv = dev->private;
	u8 reg[1] = { F020_SFR_P2 };
	u8 value[1] = { bits };
	int ret;

	down(&devpriv->sem);
	ret = dt9812_write_multiple_registers(dev, 1, reg, value);
	up(&devpriv->sem);

	return ret;
}

static void dt9812_configure_mux(struct comedi_device *dev,
				 struct dt9812_rmw_byte *rmw, int channel)
{
	struct dt9812_private *devpriv = dev->private;

	if (devpriv->device == DT9812_DEVID_DT9812_10) {
		/* In the DT9812/10V MUX is selected by P1.5-7 */
		rmw->address = F020_SFR_P1;
		rmw->and_mask = 0xe0;
		rmw->or_value = channel << 5;
	} else {
		/* In the DT9812/2.5V, internal mux is selected by bits 0:2 */
		rmw->address = F020_SFR_AMX0SL;
		rmw->and_mask = 0xff;
		rmw->or_value = channel & 0x07;
	}
}

static void dt9812_configure_gain(struct comedi_device *dev,
				  struct dt9812_rmw_byte *rmw,
				  enum dt9812_gain gain)
{
	struct dt9812_private *devpriv = dev->private;

	/* In the DT9812/10V, there is an external gain of 0.5 */
	if (devpriv->device == DT9812_DEVID_DT9812_10)
		gain <<= 1;

	rmw->address = F020_SFR_ADC0CF;
	rmw->and_mask = F020_MASK_ADC0CF_AMP0GN2 |
			F020_MASK_ADC0CF_AMP0GN1 |
			F020_MASK_ADC0CF_AMP0GN0;

	switch (gain) {
		/*
		 * 000 -> Gain =  1
		 * 001 -> Gain =  2
		 * 010 -> Gain =  4
		 * 011 -> Gain =  8
		 * 10x -> Gain = 16
		 * 11x -> Gain =  0.5
		 */
	case DT9812_GAIN_0PT5:
		rmw->or_value = F020_MASK_ADC0CF_AMP0GN2 |
				F020_MASK_ADC0CF_AMP0GN1;
		break;
	default:
		/* this should never happen, just use a gain of 1 */
	case DT9812_GAIN_1:
		rmw->or_value = 0x00;
		break;
	case DT9812_GAIN_2:
		rmw->or_value = F020_MASK_ADC0CF_AMP0GN0;
		break;
	case DT9812_GAIN_4:
		rmw->or_value = F020_MASK_ADC0CF_AMP0GN1;
		break;
	case DT9812_GAIN_8:
		rmw->or_value = F020_MASK_ADC0CF_AMP0GN1 |
				F020_MASK_ADC0CF_AMP0GN0;
		break;
	case DT9812_GAIN_16:
		rmw->or_value = F020_MASK_ADC0CF_AMP0GN2;
		break;
	}
}

static int dt9812_analog_in(struct comedi_device *dev,
			    int channel, u16 *value, enum dt9812_gain gain)
{
	struct dt9812_private *devpriv = dev->private;
	struct dt9812_rmw_byte rmw[3];
	u8 reg[3] = {
		F020_SFR_ADC0CN,
		F020_SFR_ADC0H,
		F020_SFR_ADC0L
	};
	u8 val[3];
	int ret;

	down(&devpriv->sem);

	/* 1 select the gain */
	dt9812_configure_gain(dev, &rmw[0], gain);

	/* 2 set the MUX to select the channel */
	dt9812_configure_mux(dev, &rmw[1], channel);

	/* 3 start conversion */
	rmw[2].address = F020_SFR_ADC0CN;
	rmw[2].and_mask = 0xff;
	rmw[2].or_value = F020_MASK_ADC0CN_AD0EN | F020_MASK_ADC0CN_AD0BUSY;

	ret = dt9812_rmw_multiple_registers(dev, 3, rmw);
	if (ret)
		goto exit;

	/* read the status and ADC */
	ret = dt9812_read_multiple_registers(dev, 3, reg, val);
	if (ret)
		goto exit;

	/*
	 * An ADC conversion takes 16 SAR clocks cycles, i.e. about 9us.
	 * Therefore, between the instant that AD0BUSY was set via
	 * dt9812_rmw_multiple_registers and the read of AD0BUSY via
	 * dt9812_read_multiple_registers, the conversion should be complete
	 * since these two operations require two USB transactions each taking
	 * at least a millisecond to complete.  However, lets make sure that
	 * conversion is finished.
	 */
	if ((val[0] & (F020_MASK_ADC0CN_AD0INT | F020_MASK_ADC0CN_AD0BUSY)) ==
	    F020_MASK_ADC0CN_AD0INT) {
		switch (devpriv->device) {
		case DT9812_DEVID_DT9812_10:
			/*
			 * For DT9812-10V the personality module set the
			 * encoding to 2's complement. Hence, convert it before
			 * returning it
			 */
			*value = ((val[1] << 8) | val[2]) + 0x800;
			break;
		case DT9812_DEVID_DT9812_2PT5:
			*value = (val[1] << 8) | val[2];
			break;
		}
	}

exit:
	up(&devpriv->sem);

	return ret;
}

static int dt9812_analog_out(struct comedi_device *dev, int channel, u16 value)
{
	struct dt9812_private *devpriv = dev->private;
	struct dt9812_rmw_byte rmw[3];
	int ret;

	down(&devpriv->sem);

	switch (channel) {
	case 0:
		/* 1. Set DAC mode */
		rmw[0].address = F020_SFR_DAC0CN;
		rmw[0].and_mask = 0xff;
		rmw[0].or_value = F020_MASK_DACxCN_DACxEN;

		/* 2 load low byte of DAC value first */
		rmw[1].address = F020_SFR_DAC0L;
		rmw[1].and_mask = 0xff;
		rmw[1].or_value = value & 0xff;

		/* 3 load high byte of DAC value next to latch the
			12-bit value */
		rmw[2].address = F020_SFR_DAC0H;
		rmw[2].and_mask = 0xff;
		rmw[2].or_value = (value >> 8) & 0xf;
		break;

	case 1:
		/* 1. Set DAC mode */
		rmw[0].address = F020_SFR_DAC1CN;
		rmw[0].and_mask = 0xff;
		rmw[0].or_value = F020_MASK_DACxCN_DACxEN;

		/* 2 load low byte of DAC value first */
		rmw[1].address = F020_SFR_DAC1L;
		rmw[1].and_mask = 0xff;
		rmw[1].or_value = value & 0xff;

		/* 3 load high byte of DAC value next to latch the
			12-bit value */
		rmw[2].address = F020_SFR_DAC1H;
		rmw[2].and_mask = 0xff;
		rmw[2].or_value = (value >> 8) & 0xf;
		break;
	}
	ret = dt9812_rmw_multiple_registers(dev, 3, rmw);

	up(&devpriv->sem);

	return ret;
}

static int dt9812_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	u8 bits = 0;
	int ret;

	ret = dt9812_digital_in(dev, &bits);
	if (ret)
		return ret;

	data[1] = bits;

	return insn->n;
}

static int dt9812_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		dt9812_digital_out(dev, s->state);

	data[1] = s->state;

	return insn->n;
}

static int dt9812_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	u16 val = 0;
	int ret;
	int i;

	for (i = 0; i < insn->n; i++) {
		ret = dt9812_analog_in(dev, chan, &val, DT9812_GAIN_1);
		if (ret)
			return ret;
		data[i] = val;
	}

	return insn->n;
}

static int dt9812_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct dt9812_private *devpriv = dev->private;
	int ret;

	down(&devpriv->sem);
	ret = comedi_readback_insn_read(dev, s, insn, data);
	up(&devpriv->sem);

	return ret;
}

static int dt9812_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];
		int ret;

		ret = dt9812_analog_out(dev, chan, val);
		if (ret)
			return ret;

		s->readback[chan] = val;
	}

	return insn->n;
}

static int dt9812_find_endpoints(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_host_interface *host = intf->cur_altsetting;
	struct dt9812_private *devpriv = dev->private;
	struct usb_endpoint_descriptor *ep;
	int i;

	if (host->desc.bNumEndpoints != 5) {
		dev_err(dev->class_dev, "Wrong number of endpoints\n");
		return -ENODEV;
	}

	for (i = 0; i < host->desc.bNumEndpoints; ++i) {
		int dir = -1;

		ep = &host->endpoint[i].desc;
		switch (i) {
		case 0:
			/* unused message pipe */
			dir = USB_DIR_IN;
			break;
		case 1:
			dir = USB_DIR_OUT;
			devpriv->cmd_wr.addr = ep->bEndpointAddress;
			devpriv->cmd_wr.size = le16_to_cpu(ep->wMaxPacketSize);
			break;
		case 2:
			dir = USB_DIR_IN;
			devpriv->cmd_rd.addr = ep->bEndpointAddress;
			devpriv->cmd_rd.size = le16_to_cpu(ep->wMaxPacketSize);
			break;
		case 3:
			/* unused write stream */
			dir = USB_DIR_OUT;
			break;
		case 4:
			/* unused read stream */
			dir = USB_DIR_IN;
			break;
		}
		if ((ep->bEndpointAddress & USB_DIR_IN) != dir) {
			dev_err(dev->class_dev,
				"Endpoint has wrong direction\n");
			return -ENODEV;
		}
	}
	return 0;
}

static int dt9812_reset_device(struct comedi_device *dev)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct dt9812_private *devpriv = dev->private;
	u32 serial;
	u16 vendor;
	u16 product;
	u8 tmp8;
	__le16 tmp16;
	__le32 tmp32;
	int ret;
	int i;

	ret = dt9812_read_info(dev, 0, &tmp8, sizeof(tmp8));
	if (ret) {
		/*
		 * Seems like a configuration reset is necessary if driver is
		 * reloaded while device is attached
		 */
		usb_reset_configuration(usb);
		for (i = 0; i < 10; i++) {
			ret = dt9812_read_info(dev, 1, &tmp8, sizeof(tmp8));
			if (ret == 0)
				break;
		}
		if (ret) {
			dev_err(dev->class_dev,
				"unable to reset configuration\n");
			return ret;
		}
	}

	ret = dt9812_read_info(dev, 1, &tmp16, sizeof(tmp16));
	if (ret) {
		dev_err(dev->class_dev, "failed to read vendor id\n");
		return ret;
	}
	vendor = le16_to_cpu(tmp16);

	ret = dt9812_read_info(dev, 3, &tmp16, sizeof(tmp16));
	if (ret) {
		dev_err(dev->class_dev, "failed to read product id\n");
		return ret;
	}
	product = le16_to_cpu(tmp16);

	ret = dt9812_read_info(dev, 5, &tmp16, sizeof(tmp16));
	if (ret) {
		dev_err(dev->class_dev, "failed to read device id\n");
		return ret;
	}
	devpriv->device = le16_to_cpu(tmp16);

	ret = dt9812_read_info(dev, 7, &tmp32, sizeof(tmp32));
	if (ret) {
		dev_err(dev->class_dev, "failed to read serial number\n");
		return ret;
	}
	serial = le32_to_cpu(tmp32);

	/* let the user know what node this device is now attached to */
	dev_info(dev->class_dev, "USB DT9812 (%4.4x.%4.4x.%4.4x) #0x%8.8x\n",
		 vendor, product, devpriv->device, serial);

	if (devpriv->device != DT9812_DEVID_DT9812_10 &&
	    devpriv->device != DT9812_DEVID_DT9812_2PT5) {
		dev_err(dev->class_dev, "Unsupported device!\n");
		return -EINVAL;
	}

	return 0;
}

static int dt9812_auto_attach(struct comedi_device *dev,
			      unsigned long context)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct dt9812_private *devpriv;
	struct comedi_subdevice *s;
	bool is_unipolar;
	int ret;
	int i;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	sema_init(&devpriv->sem, 1);
	usb_set_intfdata(intf, devpriv);

	ret = dt9812_find_endpoints(dev);
	if (ret)
		return ret;

	ret = dt9812_reset_device(dev);
	if (ret)
		return ret;

	is_unipolar = (devpriv->device == DT9812_DEVID_DT9812_2PT5);

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Digital Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= dt9812_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= dt9812_do_insn_bits;

	/* Analog Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND;
	s->n_chan	= 8;
	s->maxdata	= 0x0fff;
	s->range_table	= is_unipolar ? &range_unipolar2_5 : &range_bipolar10;
	s->insn_read	= dt9812_ai_insn_read;

	/* Analog Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 2;
	s->maxdata	= 0x0fff;
	s->range_table	= is_unipolar ? &range_unipolar2_5 : &range_bipolar10;
	s->insn_write	= dt9812_ao_insn_write;
	s->insn_read	= dt9812_ao_insn_read;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	for (i = 0; i < s->n_chan; i++)
		s->readback[i] = is_unipolar ? 0x0000 : 0x0800;

	return 0;
}

static void dt9812_detach(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct dt9812_private *devpriv = dev->private;

	if (!devpriv)
		return;

	down(&devpriv->sem);

	usb_set_intfdata(intf, NULL);

	up(&devpriv->sem);
}

static struct comedi_driver dt9812_driver = {
	.driver_name	= "dt9812",
	.module		= THIS_MODULE,
	.auto_attach	= dt9812_auto_attach,
	.detach		= dt9812_detach,
};

static int dt9812_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	return comedi_usb_auto_config(intf, &dt9812_driver, id->driver_info);
}

static const struct usb_device_id dt9812_usb_table[] = {
	{ USB_DEVICE(0x0867, 0x9812) },
	{ }
};
MODULE_DEVICE_TABLE(usb, dt9812_usb_table);

static struct usb_driver dt9812_usb_driver = {
	.name		= "dt9812",
	.id_table	= dt9812_usb_table,
	.probe		= dt9812_usb_probe,
	.disconnect	= comedi_usb_auto_unconfig,
};
module_comedi_usb_driver(dt9812_driver, dt9812_usb_driver);

MODULE_AUTHOR("Anders Blomdell <anders.blomdell@control.lth.se>");
MODULE_DESCRIPTION("Comedi DT9812 driver");
MODULE_LICENSE("GPL");
