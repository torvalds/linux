/*****************************************************************************
 *
 *     Author: Xilinx, Inc.
 *
 *     This program is free software; you can redistribute it and/or modify it
 *     under the terms of the GNU General Public License as published by the
 *     Free Software Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
 *     AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
 *     SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
 *     OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
 *     APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
 *     THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
 *     AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
 *     FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
 *     WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
 *     IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
 *     REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
 *     INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE.
 *
 *     (c) Copyright 2003-2008 Xilinx Inc.
 *     All rights reserved.
 *
 *     You should have received a copy of the GNU General Public License along
 *     with this program; if not, write to the Free Software Foundation, Inc.,
 *     675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#include "buffer_icap.h"

/* Indicates how many bytes will fit in a buffer. (1 BRAM) */
#define XHI_MAX_BUFFER_BYTES        2048
#define XHI_MAX_BUFFER_INTS         (XHI_MAX_BUFFER_BYTES >> 2)

/* File access and error constants */
#define XHI_DEVICE_READ_ERROR       -1
#define XHI_DEVICE_WRITE_ERROR      -2
#define XHI_BUFFER_OVERFLOW_ERROR   -3

#define XHI_DEVICE_READ             0x1
#define XHI_DEVICE_WRITE            0x0

/* Constants for checking transfer status */
#define XHI_CYCLE_DONE              0
#define XHI_CYCLE_EXECUTING         1

/* buffer_icap register offsets */

/* Size of transfer, read & write */
#define XHI_SIZE_REG_OFFSET        0x800L
/* offset into bram, read & write */
#define XHI_BRAM_OFFSET_REG_OFFSET 0x804L
/* Read not Configure, direction of transfer.  Write only */
#define XHI_RNC_REG_OFFSET         0x808L
/* Indicates transfer complete. Read only */
#define XHI_STATUS_REG_OFFSET      0x80CL

/* Constants for setting the RNC register */
#define XHI_CONFIGURE              0x0UL
#define XHI_READBACK               0x1UL

/* Constants for the Done register */
#define XHI_NOT_FINISHED           0x0UL
#define XHI_FINISHED               0x1UL

#define XHI_BUFFER_START 0

/**
 * buffer_icap_get_status - Get the contents of the status register.
 * @drvdata: a pointer to the drvdata.
 *
 * The status register contains the ICAP status and the done bit.
 *
 * D8 - cfgerr
 * D7 - dalign
 * D6 - rip
 * D5 - in_abort_l
 * D4 - Always 1
 * D3 - Always 1
 * D2 - Always 1
 * D1 - Always 1
 * D0 - Done bit
 **/
u32 buffer_icap_get_status(struct hwicap_drvdata *drvdata)
{
	return in_be32(drvdata->base_address + XHI_STATUS_REG_OFFSET);
}

/**
 * buffer_icap_get_bram - Reads data from the storage buffer bram.
 * @base_address: contains the base address of the component.
 * @offset: The word offset from which the data should be read.
 *
 * A bram is used as a configuration memory cache.  One frame of data can
 * be stored in this "storage buffer".
 **/
static inline u32 buffer_icap_get_bram(void __iomem *base_address,
		u32 offset)
{
	return in_be32(base_address + (offset << 2));
}

/**
 * buffer_icap_busy - Return true if the icap device is busy
 * @base_address: is the base address of the device
 *
 * The queries the low order bit of the status register, which
 * indicates whether the current configuration or readback operation
 * has completed.
 **/
static inline bool buffer_icap_busy(void __iomem *base_address)
{
	u32 status = in_be32(base_address + XHI_STATUS_REG_OFFSET);
	return (status & 1) == XHI_NOT_FINISHED;
}

/**
 * buffer_icap_set_size - Set the size register.
 * @base_address: is the base address of the device
 * @data: The size in bytes.
 *
 * The size register holds the number of 8 bit bytes to transfer between
 * bram and the icap (or icap to bram).
 **/
static inline void buffer_icap_set_size(void __iomem *base_address,
		u32 data)
{
	out_be32(base_address + XHI_SIZE_REG_OFFSET, data);
}

/**
 * buffer_icap_set_offset - Set the bram offset register.
 * @base_address: contains the base address of the device.
 * @data: is the value to be written to the data register.
 *
 * The bram offset register holds the starting bram address to transfer
 * data from during configuration or write data to during readback.
 **/
static inline void buffer_icap_set_offset(void __iomem *base_address,
		u32 data)
{
	out_be32(base_address + XHI_BRAM_OFFSET_REG_OFFSET, data);
}

/**
 * buffer_icap_set_rnc - Set the RNC (Readback not Configure) register.
 * @base_address: contains the base address of the device.
 * @data: is the value to be written to the data register.
 *
 * The RNC register determines the direction of the data transfer.  It
 * controls whether a configuration or readback take place.  Writing to
 * this register initiates the transfer.  A value of 1 initiates a
 * readback while writing a value of 0 initiates a configuration.
 **/
static inline void buffer_icap_set_rnc(void __iomem *base_address,
		u32 data)
{
	out_be32(base_address + XHI_RNC_REG_OFFSET, data);
}

/**
 * buffer_icap_set_bram - Write data to the storage buffer bram.
 * @base_address: contains the base address of the component.
 * @offset: The word offset at which the data should be written.
 * @data: The value to be written to the bram offset.
 *
 * A bram is used as a configuration memory cache.  One frame of data can
 * be stored in this "storage buffer".
 **/
static inline void buffer_icap_set_bram(void __iomem *base_address,
		u32 offset, u32 data)
{
	out_be32(base_address + (offset << 2), data);
}

/**
 * buffer_icap_device_read - Transfer bytes from ICAP to the storage buffer.
 * @drvdata: a pointer to the drvdata.
 * @offset: The storage buffer start address.
 * @count: The number of words (32 bit) to read from the
 *           device (ICAP).
 **/
static int buffer_icap_device_read(struct hwicap_drvdata *drvdata,
		u32 offset, u32 count)
{

	s32 retries = 0;
	void __iomem *base_address = drvdata->base_address;

	if (buffer_icap_busy(base_address))
		return -EBUSY;

	if ((offset + count) > XHI_MAX_BUFFER_INTS)
		return -EINVAL;

	/* setSize count*4 to get bytes. */
	buffer_icap_set_size(base_address, (count << 2));
	buffer_icap_set_offset(base_address, offset);
	buffer_icap_set_rnc(base_address, XHI_READBACK);

	while (buffer_icap_busy(base_address)) {
		retries++;
		if (retries > XHI_MAX_RETRIES)
			return -EBUSY;
	}
	return 0;

};

/**
 * buffer_icap_device_write - Transfer bytes from ICAP to the storage buffer.
 * @drvdata: a pointer to the drvdata.
 * @offset: The storage buffer start address.
 * @count: The number of words (32 bit) to read from the
 *           device (ICAP).
 **/
static int buffer_icap_device_write(struct hwicap_drvdata *drvdata,
		u32 offset, u32 count)
{

	s32 retries = 0;
	void __iomem *base_address = drvdata->base_address;

	if (buffer_icap_busy(base_address))
		return -EBUSY;

	if ((offset + count) > XHI_MAX_BUFFER_INTS)
		return -EINVAL;

	/* setSize count*4 to get bytes. */
	buffer_icap_set_size(base_address, count << 2);
	buffer_icap_set_offset(base_address, offset);
	buffer_icap_set_rnc(base_address, XHI_CONFIGURE);

	while (buffer_icap_busy(base_address)) {
		retries++;
		if (retries > XHI_MAX_RETRIES)
			return -EBUSY;
	}
	return 0;

};

/**
 * buffer_icap_reset - Reset the logic of the icap device.
 * @drvdata: a pointer to the drvdata.
 *
 * Writing to the status register resets the ICAP logic in an internal
 * version of the core.  For the version of the core published in EDK,
 * this is a noop.
 **/
void buffer_icap_reset(struct hwicap_drvdata *drvdata)
{
    out_be32(drvdata->base_address + XHI_STATUS_REG_OFFSET, 0xFEFE);
}

/**
 * buffer_icap_set_configuration - Load a partial bitstream from system memory.
 * @drvdata: a pointer to the drvdata.
 * @data: Kernel address of the partial bitstream.
 * @size: the size of the partial bitstream in 32 bit words.
 **/
int buffer_icap_set_configuration(struct hwicap_drvdata *drvdata, u32 *data,
			     u32 size)
{
	int status;
	s32 buffer_count = 0;
	s32 num_writes = 0;
	bool dirty = false;
	u32 i;
	void __iomem *base_address = drvdata->base_address;

	/* Loop through all the data */
	for (i = 0, buffer_count = 0; i < size; i++) {

		/* Copy data to bram */
		buffer_icap_set_bram(base_address, buffer_count, data[i]);
		dirty = true;

		if (buffer_count < XHI_MAX_BUFFER_INTS - 1) {
			buffer_count++;
			continue;
		}

		/* Write data to ICAP */
		status = buffer_icap_device_write(
				drvdata,
				XHI_BUFFER_START,
				XHI_MAX_BUFFER_INTS);
		if (status != 0) {
			/* abort. */
			buffer_icap_reset(drvdata);
			return status;
		}

		buffer_count = 0;
		num_writes++;
		dirty = false;
	}

	/* Write unwritten data to ICAP */
	if (dirty) {
		/* Write data to ICAP */
		status = buffer_icap_device_write(drvdata, XHI_BUFFER_START,
					     buffer_count);
		if (status != 0) {
			/* abort. */
			buffer_icap_reset(drvdata);
		}
		return status;
	}

	return 0;
};

/**
 * buffer_icap_get_configuration - Read configuration data from the device.
 * @drvdata: a pointer to the drvdata.
 * @data: Address of the data representing the partial bitstream
 * @size: the size of the partial bitstream in 32 bit words.
 **/
int buffer_icap_get_configuration(struct hwicap_drvdata *drvdata, u32 *data,
			     u32 size)
{
	int status;
	s32 buffer_count = 0;
	s32 read_count = 0;
	u32 i;
	void __iomem *base_address = drvdata->base_address;

	/* Loop through all the data */
	for (i = 0, buffer_count = XHI_MAX_BUFFER_INTS; i < size; i++) {
		if (buffer_count == XHI_MAX_BUFFER_INTS) {
			u32 words_remaining = size - i;
			u32 words_to_read =
				words_remaining <
				XHI_MAX_BUFFER_INTS ? words_remaining :
				XHI_MAX_BUFFER_INTS;

			/* Read data from ICAP */
			status = buffer_icap_device_read(
					drvdata,
					XHI_BUFFER_START,
					words_to_read);
			if (status != 0) {
				/* abort. */
				buffer_icap_reset(drvdata);
				return status;
			}

			buffer_count = 0;
			read_count++;
		}

		/* Copy data from bram */
		data[i] = buffer_icap_get_bram(base_address, buffer_count);
		buffer_count++;
	}

	return 0;
};
