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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#include "../comedidev.h"

#define DT9812_DIAGS_BOARD_INFO_ADDR	0xFBFF
#define DT9812_MAX_WRITE_CMD_PIPE_SIZE	32
#define DT9812_MAX_READ_CMD_PIPE_SIZE	32

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
	DT9812_DEVID_DT9812_10,	/*  8    2   8   8   1  +/- 10V */
	DT9812_DEVID_DT9812_2PT5,	/* 8    2   8   8   1  0-2.44V */
#if 0
	DT9812_DEVID_DT9813,	/*  16   2   4   4   1  +/- 10V */
	DT9812_DEVID_DT9814	/*  24   2   0   0   1  +/- 10V */
#endif
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
	u16 numbytes;
	u16 address;
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
	u32 cmd;
	union {
		struct dt9812_flash_data flash_data_info;
		struct dt9812_read_multi read_multi_info;
		struct dt9812_write_multi write_multi_info;
		struct dt9812_rmw_multi rmw_multi_info;
	} u;
#if 0
	WRITE_BYTE_INFO WriteByteInfo;
	READ_BYTE_INFO ReadByteInfo;
	WRITE_MULTI_INFO WriteMultiInfo;
	READ_MULTI_INFO ReadMultiInfo;
	RMW_BYTE_INFO RMWByteInfo;
	RMW_MULTI_INFO RMWMultiInfo;
	DAC_THRESHOLD_INFO DacThresholdInfo;
	INT_ON_CHANGE_MASK_INFO IntOnChangeMaskInfo;
	CGL_INFO CglInfo;
	SUBSYSTEM_INFO SubsystemInfo;
	CAL_POT_CMD CalPotCmd;
	WRITE_DEV_BYTE_INFO WriteDevByteInfo;
	READ_DEV_BYTE_INFO ReadDevByteInfo;
	WRITE_DEV_MULTI_INFO WriteDevMultiInfo;
	READ_DEV_MULTI_INFO ReadDevMultiInfo;
	READ_SINGLE_VALUE_INFO ReadSingleValueInfo;
	WRITE_SINGLE_VALUE_INFO WriteSingleValueInfo;
#endif
};

#define DT9812_NUM_SLOTS	16

static DEFINE_SEMAPHORE(dt9812_mutex);

struct usb_dt9812 {
	struct slot_dt9812 *slot;
	struct usb_device *udev;
	struct usb_interface *interface;
	u16 vendor;
	u16 product;
	u16 device;
	u32 serial;
	struct {
		__u8 addr;
		size_t size;
	} message_pipe, command_write, command_read, write_stream, read_stream;
	struct kref kref;
	u16 analog_out_shadow[2];
	u8 digital_out_shadow;
};

struct dt9812_private {
	struct slot_dt9812 *slot;
};

struct slot_dt9812 {
	struct semaphore mutex;
	struct usb_dt9812 *usb;
	struct dt9812_private *devpriv;
};

static struct slot_dt9812 dt9812[DT9812_NUM_SLOTS];

static inline struct usb_dt9812 *to_dt9812_dev(struct kref *d)
{
	return container_of(d, struct usb_dt9812, kref);
}

static void dt9812_delete(struct kref *kref)
{
	struct usb_dt9812 *dev = to_dt9812_dev(kref);

	usb_put_dev(dev->udev);
	kfree(dev);
}

static int dt9812_read_info(struct usb_dt9812 *dev, int offset, void *buf,
			    size_t buf_size)
{
	struct usb_device *usb = dev->udev;
	struct dt9812_usb_cmd cmd;
	int count, ret;

	cmd.cmd = cpu_to_le32(DT9812_R_FLASH_DATA);
	cmd.u.flash_data_info.address =
	    cpu_to_le16(DT9812_DIAGS_BOARD_INFO_ADDR + offset);
	cmd.u.flash_data_info.numbytes = cpu_to_le16(buf_size);

	/* DT9812 only responds to 32 byte writes!! */
	ret = usb_bulk_msg(usb, usb_sndbulkpipe(usb, dev->command_write.addr),
			   &cmd, 32, &count, HZ * 1);
	if (ret)
		return ret;

	return usb_bulk_msg(usb, usb_rcvbulkpipe(usb, dev->command_read.addr),
			    buf, buf_size, &count, HZ * 1);
}

static int dt9812_read_multiple_registers(struct usb_dt9812 *dev, int reg_count,
					  u8 *address, u8 *value)
{
	struct usb_device *usb = dev->udev;
	struct dt9812_usb_cmd cmd;
	int i, count, ret;

	cmd.cmd = cpu_to_le32(DT9812_R_MULTI_BYTE_REG);
	cmd.u.read_multi_info.count = reg_count;
	for (i = 0; i < reg_count; i++)
		cmd.u.read_multi_info.address[i] = address[i];

	/* DT9812 only responds to 32 byte writes!! */
	ret = usb_bulk_msg(usb, usb_sndbulkpipe(usb, dev->command_write.addr),
			   &cmd, 32, &count, HZ * 1);
	if (ret)
		return ret;

	return usb_bulk_msg(usb, usb_rcvbulkpipe(usb, dev->command_read.addr),
			    value, reg_count, &count, HZ * 1);
}

static int dt9812_write_multiple_registers(struct usb_dt9812 *dev,
					   int reg_count, u8 *address,
					   u8 *value)
{
	struct usb_device *usb = dev->udev;
	struct dt9812_usb_cmd cmd;
	int i, count;

	cmd.cmd = cpu_to_le32(DT9812_W_MULTI_BYTE_REG);
	cmd.u.read_multi_info.count = reg_count;
	for (i = 0; i < reg_count; i++) {
		cmd.u.write_multi_info.write[i].address = address[i];
		cmd.u.write_multi_info.write[i].value = value[i];
	}

	/* DT9812 only responds to 32 byte writes!! */
	return usb_bulk_msg(usb, usb_sndbulkpipe(usb, dev->command_write.addr),
			    &cmd, 32, &count, HZ * 1);
}

static int dt9812_rmw_multiple_registers(struct usb_dt9812 *dev, int reg_count,
					 struct dt9812_rmw_byte *rmw)
{
	struct usb_device *usb = dev->udev;
	struct dt9812_usb_cmd cmd;
	int i, count;

	cmd.cmd = cpu_to_le32(DT9812_RMW_MULTI_BYTE_REG);
	cmd.u.rmw_multi_info.count = reg_count;
	for (i = 0; i < reg_count; i++)
		cmd.u.rmw_multi_info.rmw[i] = rmw[i];

	/* DT9812 only responds to 32 byte writes!! */
	return usb_bulk_msg(usb, usb_sndbulkpipe(usb, dev->command_write.addr),
			    &cmd, 32, &count, HZ * 1);
}

static int dt9812_digital_in(struct comedi_device *dev, u8 *bits)
{
	struct dt9812_private *devpriv = dev->private;
	struct slot_dt9812 *slot = devpriv->slot;
	int ret = -ENODEV;

	down(&slot->mutex);
	if (slot->usb) {
		u8 reg[2] = { F020_SFR_P3, F020_SFR_P1 };
		u8 value[2];

		ret = dt9812_read_multiple_registers(slot->usb, 2, reg, value);
		if (ret == 0) {
			/*
			 * bits 0-6 in F020_SFR_P3 are bits 0-6 in the digital
			 * input port bit 3 in F020_SFR_P1 is bit 7 in the
			 * digital input port
			 */
			*bits = (value[0] & 0x7f) | ((value[1] & 0x08) << 4);
		}
	}
	up(&slot->mutex);

	return ret;
}

static int dt9812_digital_out(struct slot_dt9812 *slot, u8 bits)
{
	int result = -ENODEV;

	down(&slot->mutex);
	if (slot->usb) {
		u8 reg[1];
		u8 value[1];

		reg[0] = F020_SFR_P2;
		value[0] = bits;
		result = dt9812_write_multiple_registers(slot->usb, 1, reg,
							 value);
		slot->usb->digital_out_shadow = bits;
	}
	up(&slot->mutex);
	return result;
}

static int dt9812_digital_out_shadow(struct slot_dt9812 *slot, u8 *bits)
{
	int result = -ENODEV;

	down(&slot->mutex);
	if (slot->usb) {
		*bits = slot->usb->digital_out_shadow;
		result = 0;
	}
	up(&slot->mutex);
	return result;
}

static void dt9812_configure_mux(struct usb_dt9812 *dev,
				 struct dt9812_rmw_byte *rmw, int channel)
{
	if (dev->device == DT9812_DEVID_DT9812_10) {
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

static void dt9812_configure_gain(struct usb_dt9812 *dev,
				  struct dt9812_rmw_byte *rmw,
				  enum dt9812_gain gain)
{
	if (dev->device == DT9812_DEVID_DT9812_10) {
		/* In the DT9812/10V, there is an external gain of 0.5 */
		gain <<= 1;
	}

	rmw->address = F020_SFR_ADC0CF;
	rmw->and_mask = F020_MASK_ADC0CF_AMP0GN2 |
	    F020_MASK_ADC0CF_AMP0GN1 | F020_MASK_ADC0CF_AMP0GN0;
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
	default:
		dev_err(&dev->interface->dev, "Illegal gain %d\n", gain);

	}
}

static int dt9812_analog_in(struct slot_dt9812 *slot, int channel, u16 *value,
			    enum dt9812_gain gain)
{
	struct dt9812_rmw_byte rmw[3];
	u8 reg[3] = {
		F020_SFR_ADC0CN,
		F020_SFR_ADC0H,
		F020_SFR_ADC0L
	};
	u8 val[3];
	int result = -ENODEV;

	down(&slot->mutex);
	if (!slot->usb)
		goto exit;

	/* 1 select the gain */
	dt9812_configure_gain(slot->usb, &rmw[0], gain);

	/* 2 set the MUX to select the channel */
	dt9812_configure_mux(slot->usb, &rmw[1], channel);

	/* 3 start conversion */
	rmw[2].address = F020_SFR_ADC0CN;
	rmw[2].and_mask = 0xff;
	rmw[2].or_value = F020_MASK_ADC0CN_AD0EN | F020_MASK_ADC0CN_AD0BUSY;

	result = dt9812_rmw_multiple_registers(slot->usb, 3, rmw);
	if (result)
		goto exit;

	/* read the status and ADC */
	result = dt9812_read_multiple_registers(slot->usb, 3, reg, val);
	if (result)
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
		switch (slot->usb->device) {
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
	up(&slot->mutex);
	return result;
}

static int dt9812_analog_out_shadow(struct slot_dt9812 *slot, int channel,
				    u16 *value)
{
	int result = -ENODEV;

	down(&slot->mutex);
	if (slot->usb) {
		*value = slot->usb->analog_out_shadow[channel];
		result = 0;
	}
	up(&slot->mutex);

	return result;
}

static int dt9812_analog_out(struct slot_dt9812 *slot, int channel, u16 value)
{
	int result = -ENODEV;

	down(&slot->mutex);
	if (slot->usb) {
		struct dt9812_rmw_byte rmw[3];

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
		result = dt9812_rmw_multiple_registers(slot->usb, 3, rmw);
		slot->usb->analog_out_shadow[channel] = value;
	}
	up(&slot->mutex);

	return result;
}

static int dt9812_di_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	unsigned int channel = CR_CHAN(insn->chanspec);
	int n;
	u8 bits = 0;

	dt9812_digital_in(dev, &bits);
	for (n = 0; n < insn->n; n++)
		data[n] = ((1 << channel) & bits) != 0;
	return n;
}

static int dt9812_do_winsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct dt9812_private *devpriv = dev->private;
	unsigned int channel = CR_CHAN(insn->chanspec);
	int n;
	u8 bits = 0;

	dt9812_digital_out_shadow(devpriv->slot, &bits);
	for (n = 0; n < insn->n; n++) {
		u8 mask = 1 << channel;

		bits &= ~mask;
		if (data[n])
			bits |= mask;
	}
	dt9812_digital_out(devpriv->slot, bits);
	return n;
}

static int dt9812_ai_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct dt9812_private *devpriv = dev->private;
	unsigned int channel = CR_CHAN(insn->chanspec);
	int n;

	for (n = 0; n < insn->n; n++) {
		u16 value = 0;

		dt9812_analog_in(devpriv->slot, channel, &value, DT9812_GAIN_1);
		data[n] = value;
	}
	return n;
}

static int dt9812_ao_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct dt9812_private *devpriv = dev->private;
	unsigned int channel = CR_CHAN(insn->chanspec);
	int n;
	u16 value;

	for (n = 0; n < insn->n; n++) {
		value = 0;
		dt9812_analog_out_shadow(devpriv->slot, channel, &value);
		data[n] = value;
	}
	return n;
}

static int dt9812_ao_winsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct dt9812_private *devpriv = dev->private;
	unsigned int channel = CR_CHAN(insn->chanspec);
	int n;

	for (n = 0; n < insn->n; n++)
		dt9812_analog_out(devpriv->slot, channel, data[n]);
	return n;
}

static int dt9812_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct slot_dt9812 *slot = NULL;
	struct dt9812_private *devpriv;
	int i;
	struct comedi_subdevice *s;
	bool range_2_5;
	int ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	down(&dt9812_mutex);

	/*
	 * Find the first unused slot for the comedi device
	 * that has a usb device connected.
	 */
	for (i = 0; i < DT9812_NUM_SLOTS; i++) {
		if (dt9812[i].usb && !dt9812[i].devpriv) {
			slot = &dt9812[i];
			break;
		}
	}
	if (!slot) {
		up(&dt9812_mutex);
		return -ENODEV;
	}

	down(&slot->mutex);
	slot->devpriv = devpriv;
	devpriv->slot = slot;
	range_2_5 = (slot->usb->device == DT9812_DEVID_DT9812_2PT5);
	up(&slot->mutex);

	up(&dt9812_mutex);

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* digital input subdevice */
	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_read = &dt9812_di_rinsn;

	/* digital output subdevice */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITEABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_write = &dt9812_do_winsn;

	/* analog input subdevice */
	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = 8;
	s->maxdata = 4095;
	s->range_table = range_2_5 ? &range_unipolar2_5 : &range_bipolar10;
	s->insn_read = &dt9812_ai_rinsn;

	/* analog output subdevice */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITEABLE;
	s->n_chan = 0;
	s->maxdata = 4095;
	s->range_table = range_2_5 ? &range_unipolar2_5 : &range_bipolar10;
	s->insn_write = &dt9812_ao_winsn;
	s->insn_read = &dt9812_ao_rinsn;

	dev_info(dev->class_dev, "successfully attached to dt9812.\n");

	return 0;
}

static void dt9812_detach(struct comedi_device *dev)
{
	struct dt9812_private *devpriv = dev->private;

	if (devpriv && devpriv->slot)
		devpriv->slot = NULL;
}

static struct comedi_driver dt9812_comedi_driver = {
	.module = THIS_MODULE,
	.driver_name = "dt9812",
	.attach = dt9812_attach,
	.detach = dt9812_detach,
};

static int dt9812_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	struct slot_dt9812 *slot = NULL;
	struct usb_dt9812 *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int retval = -ENOMEM;
	int i;
	u8 fw;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		goto error;

	kref_init(&dev->kref);

	down(&dt9812_mutex);

	/* Find an empty slot for the usb device */
	for (i = 0; i < DT9812_NUM_SLOTS; i++) {
		if (!dt9812[i].usb) {
			slot = &dt9812[i];
			break;
		}
	}
	if (!slot) {
		up(&dt9812_mutex);
		retval = -ENODEV;
		goto error;
	}

	down(&slot->mutex);
	slot->usb = dev;
	dev->slot = slot;
	up(&slot->mutex);

	up(&dt9812_mutex);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* Check endpoints */
	iface_desc = interface->cur_altsetting;

	if (iface_desc->desc.bNumEndpoints != 5) {
		dev_err(&interface->dev, "Wrong number of endpoints.\n");
		retval = -ENODEV;
		goto error;
	}

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		int direction = -1;
		endpoint = &iface_desc->endpoint[i].desc;
		switch (i) {
		case 0:
			direction = USB_DIR_IN;
			dev->message_pipe.addr = endpoint->bEndpointAddress;
			dev->message_pipe.size =
			    le16_to_cpu(endpoint->wMaxPacketSize);

			break;
		case 1:
			direction = USB_DIR_OUT;
			dev->command_write.addr = endpoint->bEndpointAddress;
			dev->command_write.size =
			    le16_to_cpu(endpoint->wMaxPacketSize);
			break;
		case 2:
			direction = USB_DIR_IN;
			dev->command_read.addr = endpoint->bEndpointAddress;
			dev->command_read.size =
			    le16_to_cpu(endpoint->wMaxPacketSize);
			break;
		case 3:
			direction = USB_DIR_OUT;
			dev->write_stream.addr = endpoint->bEndpointAddress;
			dev->write_stream.size =
			    le16_to_cpu(endpoint->wMaxPacketSize);
			break;
		case 4:
			direction = USB_DIR_IN;
			dev->read_stream.addr = endpoint->bEndpointAddress;
			dev->read_stream.size =
			    le16_to_cpu(endpoint->wMaxPacketSize);
			break;
		}
		if ((endpoint->bEndpointAddress & USB_DIR_IN) != direction) {
			dev_err(&interface->dev,
				"Endpoint has wrong direction.\n");
			retval = -ENODEV;
			goto error;
		}
	}
	if (dt9812_read_info(dev, 0, &fw, sizeof(fw)) != 0) {
		/*
		 * Seems like a configuration reset is necessary if driver is
		 * reloaded while device is attached
		 */
		usb_reset_configuration(dev->udev);
		for (i = 0; i < 10; i++) {
			retval = dt9812_read_info(dev, 1, &fw, sizeof(fw));
			if (retval == 0) {
				dev_info(&interface->dev,
					 "usb_reset_configuration succeeded "
					 "after %d iterations\n", i);
				break;
			}
		}
	}

	if (dt9812_read_info(dev, 1, &dev->vendor, sizeof(dev->vendor)) != 0) {
		dev_err(&interface->dev, "Failed to read vendor.\n");
		retval = -ENODEV;
		goto error;
	}
	if (dt9812_read_info(dev, 3, &dev->product, sizeof(dev->product)) != 0) {
		dev_err(&interface->dev, "Failed to read product.\n");
		retval = -ENODEV;
		goto error;
	}
	if (dt9812_read_info(dev, 5, &dev->device, sizeof(dev->device)) != 0) {
		dev_err(&interface->dev, "Failed to read device.\n");
		retval = -ENODEV;
		goto error;
	}
	if (dt9812_read_info(dev, 7, &dev->serial, sizeof(dev->serial)) != 0) {
		dev_err(&interface->dev, "Failed to read serial.\n");
		retval = -ENODEV;
		goto error;
	}

	dev->vendor = le16_to_cpu(dev->vendor);
	dev->product = le16_to_cpu(dev->product);
	dev->device = le16_to_cpu(dev->device);
	dev->serial = le32_to_cpu(dev->serial);
	switch (dev->device) {
	case DT9812_DEVID_DT9812_10:
		dev->analog_out_shadow[0] = 0x0800;
		dev->analog_out_shadow[1] = 0x800;
		break;
	case DT9812_DEVID_DT9812_2PT5:
		dev->analog_out_shadow[0] = 0x0000;
		dev->analog_out_shadow[1] = 0x0000;
		break;
	}
	dev->digital_out_shadow = 0;

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev, "USB DT9812 (%4.4x.%4.4x.%4.4x) #0x%8.8x\n",
		 dev->vendor, dev->product, dev->device, dev->serial);

	return 0;

error:
	if (dev)
		kref_put(&dev->kref, dt9812_delete);
	return retval;
}

static void dt9812_disconnect(struct usb_interface *interface)
{
	struct usb_dt9812 *dev;
	int minor = interface->minor;

	down(&dt9812_mutex);
	dev = usb_get_intfdata(interface);
	if (dev->slot) {
		down(&dev->slot->mutex);
		dev->slot->usb = NULL;
		up(&dev->slot->mutex);
		dev->slot = NULL;
	}
	usb_set_intfdata(interface, NULL);
	up(&dt9812_mutex);

	/* queue final destruction */
	kref_put(&dev->kref, dt9812_delete);

	dev_info(&interface->dev, "USB Dt9812 #%d now disconnected\n", minor);
}

static const struct usb_device_id dt9812_table[] = {
	{ USB_DEVICE(0x0867, 0x9812) },
	{ }
};
MODULE_DEVICE_TABLE(usb, dt9812_table);

static struct usb_driver dt9812_usb_driver = {
	.name		= "dt9812",
	.id_table	= dt9812_table,
	.probe		= dt9812_probe,
	.disconnect	= dt9812_disconnect,
};

static int __init usb_dt9812_init(void)
{
	int i;

	/* Initialize all driver slots */
	for (i = 0; i < DT9812_NUM_SLOTS; i++) {
		sema_init(&dt9812[i].mutex, 1);
		dt9812[i].usb = NULL;
		dt9812[i].devpriv = NULL;
	}

	return comedi_usb_driver_register(&dt9812_comedi_driver,
						&dt9812_usb_driver);
}

static void __exit usb_dt9812_exit(void)
{
	comedi_usb_driver_unregister(&dt9812_comedi_driver, &dt9812_usb_driver);
}

module_init(usb_dt9812_init);
module_exit(usb_dt9812_exit);

MODULE_AUTHOR("Anders Blomdell <anders.blomdell@control.lth.se>");
MODULE_DESCRIPTION("Comedi DT9812 driver");
MODULE_LICENSE("GPL");
