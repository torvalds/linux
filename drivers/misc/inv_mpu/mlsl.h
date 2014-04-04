/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */

#ifndef __MLSL_H__
#define __MLSL_H__

/**
 *  @defgroup   MLSL
 *  @brief      Motion Library - Serial Layer.
 *              The Motion Library System Layer provides the Motion Library
 *              with the communication interface to the hardware.
 *
 *              The communication interface is assumed to support serial
 *              transfers in burst of variable length up to
 *              SERIAL_MAX_TRANSFER_SIZE.
 *              The default value for SERIAL_MAX_TRANSFER_SIZE is 128 bytes.
 *              Transfers of length greater than SERIAL_MAX_TRANSFER_SIZE, will
 *              be subdivided in smaller transfers of length <=
 *              SERIAL_MAX_TRANSFER_SIZE.
 *              The SERIAL_MAX_TRANSFER_SIZE definition can be modified to
 *              overcome any host processor transfer size limitation down to
 *              1 B, the minimum.
 *              An higher value for SERIAL_MAX_TRANSFER_SIZE will favor
 *              performance and efficiency while requiring higher resource usage
 *              (mostly buffering). A smaller value will increase overhead and
 *              decrease efficiency but allows to operate with more resource
 *              constrained processor and master serial controllers.
 *              The SERIAL_MAX_TRANSFER_SIZE definition can be found in the
 *              mlsl.h header file and master serial controllers.
 *              The SERIAL_MAX_TRANSFER_SIZE definition can be found in the
 *              mlsl.h header file.
 *
 *  @{
 *      @file   mlsl.h
 *      @brief  The Motion Library System Layer.
 *
 */

#include "mltypes.h"
#include <linux/mpu.h>


/*
 * NOTE : to properly support Yamaha compass reads,
 *	  the max transfer size should be at least 9 B.
 *	  Length in bytes, typically a power of 2 >= 2
 */
#define SERIAL_MAX_TRANSFER_SIZE 128


/**
 *  inv_serial_single_write() - used to write a single byte of data.
 *  @sl_handle		pointer to the serial device used for the communication.
 *  @slave_addr		I2C slave address of device.
 *  @register_addr	Register address to write.
 *  @data		Single byte of data to write.
 *
 *	It is called by the MPL to write a single byte of data to the MPU.
 *
 *  returns INV_SUCCESS if successful, a non-zero error code otherwise.
 */
int inv_serial_single_write(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned char register_addr,
	unsigned char data);

/**
 *  inv_serial_write() - used to write multiple bytes of data to registers.
 *  @sl_handle	a file handle to the serial device used for the communication.
 *  @slave_addr	I2C slave address of device.
 *  @register_addr	Register address to write.
 *  @length	Length of burst of data.
 *  @data	Pointer to block of data.
 *
 *  returns INV_SUCCESS if successful, a non-zero error code otherwise.
 */
int inv_serial_write(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short length,
	unsigned char const *data);

/**
 *  inv_serial_read() - used to read multiple bytes of data from registers.
 *  @sl_handle	a file handle to the serial device used for the communication.
 *  @slave_addr	I2C slave address of device.
 *  @register_addr	Register address to read.
 *  @length	Length of burst of data.
 *  @data	Pointer to block of data.
 *
 *  returns INV_SUCCESS == 0 if successful; a non-zero error code otherwise.
 */
int inv_serial_read(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned char register_addr,
	unsigned short length,
	unsigned char *data);

/**
 *  inv_serial_read_mem() - used to read multiple bytes of data from the memory.
 *	    This should be sent by I2C or SPI.
 *
 *  @sl_handle	a file handle to the serial device used for the communication.
 *  @slave_addr	I2C slave address of device.
 *  @mem_addr	The location in the memory to read from.
 *  @length	Length of burst data.
 *  @data	Pointer to block of data.
 *
 *  returns INV_SUCCESS == 0 if successful; a non-zero error code otherwise.
 */
int inv_serial_read_mem(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short mem_addr,
	unsigned short length,
	unsigned char *data);

/**
 *  inv_serial_write_mem() - used to write multiple bytes of data to the memory.
 *  @sl_handle	a file handle to the serial device used for the communication.
 *  @slave_addr	I2C slave address of device.
 *  @mem_addr	The location in the memory to write to.
 *  @length	Length of burst data.
 *  @data	Pointer to block of data.
 *
 *  returns INV_SUCCESS == 0 if successful; a non-zero error code otherwise.
 */
int inv_serial_write_mem(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short mem_addr,
	unsigned short length,
	unsigned char const *data);

/**
 *  inv_serial_read_fifo() - used to read multiple bytes of data from the fifo.
 *  @sl_handle	a file handle to the serial device used for the communication.
 *  @slave_addr	I2C slave address of device.
 *  @length	Length of burst of data.
 *  @data	Pointer to block of data.
 *
 *  returns INV_SUCCESS == 0 if successful; a non-zero error code otherwise.
 */
int inv_serial_read_fifo(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short length,
	unsigned char *data);

/**
 *  inv_serial_write_fifo() - used to write multiple bytes of data to the fifo.
 *  @sl_handle	a file handle to the serial device used for the communication.
 *  @slave_addr	I2C slave address of device.
 *  @length	Length of burst of data.
 *  @data	Pointer to block of data.
 *
 *  returns INV_SUCCESS == 0 if successful; a non-zero error code otherwise.
 */
int inv_serial_write_fifo(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short length,
	unsigned char const *data);

/**
 * @}
 */
#endif				/* __MLSL_H__ */
