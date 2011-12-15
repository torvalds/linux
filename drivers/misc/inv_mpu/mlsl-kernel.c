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

#include "mlsl.h"
#include <linux/i2c.h>
#include "log.h"
#include "mpu3050.h"

#define MPU_I2C_RATE 200*1000
static int inv_i2c_write(struct i2c_adapter *i2c_adap,
			    unsigned char address,
			    unsigned int len, unsigned char const *data)
{
	struct i2c_msg msgs[1];
	int res;

	if (!data || !i2c_adap) {
		LOG_RESULT_LOCATION(-EINVAL);
		return -EINVAL;
	}

	msgs[0].addr = address;
	msgs[0].flags = 0;	/* write */
	msgs[0].buf = (unsigned char *)data;
	msgs[0].len = len;
	msgs[0].scl_rate = MPU_I2C_RATE;
	//msgs[0].udelay = 200;

	res = i2c_transfer(i2c_adap, msgs, 1);
	if (res == 1)
		return 0;
	else if(res == 0)
		return -EBUSY;
	else
		return res;

}

static int inv_i2c_write_register(struct i2c_adapter *i2c_adap,
				     unsigned char address,
				     unsigned char reg, unsigned char value)
{
	unsigned char data[2];

	data[0] = reg;
	data[1] = value;
	return inv_i2c_write(i2c_adap, address, 2, data);
}

static int inv_i2c_read(struct i2c_adapter *i2c_adap,
			   unsigned char address, unsigned char reg,
			   unsigned int len, unsigned char *data)
{
	struct i2c_msg msgs[2];
	int res;

	if (!data || !i2c_adap) {
		LOG_RESULT_LOCATION(-EINVAL);
		return -EINVAL;
	}

	msgs[0].addr = address;
	msgs[0].flags = 0;	/* write */
	msgs[0].buf = &reg;
	msgs[0].len = 1;
	msgs[0].scl_rate = MPU_I2C_RATE;
	//msgs[0].udelay = 200;
	
	msgs[1].addr = address;
	msgs[1].flags = I2C_M_RD;
	msgs[1].buf = data;
	msgs[1].len = len;
	msgs[1].scl_rate = MPU_I2C_RATE;	
	//msgs[1].udelay = 200;

	res = i2c_transfer(i2c_adap, msgs, 2);
	if (res == 2)
		return 0;
	else if(res == 0)
		return -EBUSY;
	else
		return res;

}

static int mpu_memory_read(struct i2c_adapter *i2c_adap,
			   unsigned char mpu_addr,
			   unsigned short mem_addr,
			   unsigned int len, unsigned char *data)
{
	unsigned char bank[2];
	unsigned char addr[2];
	unsigned char buf;

	struct i2c_msg msgs[4];
	int res;

	if (!data || !i2c_adap) {
		LOG_RESULT_LOCATION(-EINVAL);
		return -EINVAL;
	}

	bank[0] = MPUREG_BANK_SEL;
	bank[1] = mem_addr >> 8;

	addr[0] = MPUREG_MEM_START_ADDR;
	addr[1] = mem_addr & 0xFF;

	buf = MPUREG_MEM_R_W;

	/* write message */
	msgs[0].addr = mpu_addr;
	msgs[0].flags = 0;
	msgs[0].buf = bank;
	msgs[0].len = sizeof(bank);
	msgs[0].scl_rate = MPU_I2C_RATE;
	//msgs[0].udelay = 200;

	msgs[1].addr = mpu_addr;
	msgs[1].flags = 0;
	msgs[1].buf = addr;
	msgs[1].len = sizeof(addr);
	msgs[1].scl_rate = MPU_I2C_RATE;
	//msgs[1].udelay = 200;

	msgs[2].addr = mpu_addr;
	msgs[2].flags = 0;
	msgs[2].buf = &buf;
	msgs[2].len = 1;
	msgs[2].scl_rate = MPU_I2C_RATE;
	//msgs[2].udelay = 200;

	msgs[3].addr = mpu_addr;
	msgs[3].flags = I2C_M_RD;
	msgs[3].buf = data;
	msgs[3].len = len;
	msgs[3].scl_rate = MPU_I2C_RATE;
	//msgs[3].udelay = 200;

	res = i2c_transfer(i2c_adap, msgs, 4);
	if (res == 4)
		return 0;
	else if(res == 0)
		return -EBUSY;
	else
		return res;
}

static int mpu_memory_write(struct i2c_adapter *i2c_adap,
			    unsigned char mpu_addr,
			    unsigned short mem_addr,
			    unsigned int len, unsigned char const *data)
{
	unsigned char bank[2];
	unsigned char addr[2];
	unsigned char buf[513];

	struct i2c_msg msgs[3];
	int res;

	if (!data || !i2c_adap) {
		LOG_RESULT_LOCATION(-EINVAL);
		return -EINVAL;
	}
	if (len >= (sizeof(buf) - 1)) {
		LOG_RESULT_LOCATION(-ENOMEM);
		return -ENOMEM;
	}

	bank[0] = MPUREG_BANK_SEL;
	bank[1] = mem_addr >> 8;

	addr[0] = MPUREG_MEM_START_ADDR;
	addr[1] = mem_addr & 0xFF;

	buf[0] = MPUREG_MEM_R_W;
	memcpy(buf + 1, data, len);

	/* write message */
	msgs[0].addr = mpu_addr;
	msgs[0].flags = 0;
	msgs[0].buf = bank;
	msgs[0].len = sizeof(bank);
	msgs[0].scl_rate = MPU_I2C_RATE;
	//msgs[0].udelay = 200; 

	msgs[1].addr = mpu_addr;
	msgs[1].flags = 0;
	msgs[1].buf = addr;
	msgs[1].len = sizeof(addr);
	msgs[1].scl_rate = MPU_I2C_RATE;
	//msgs[1].udelay = 200;

	msgs[2].addr = mpu_addr;
	msgs[2].flags = 0;
	msgs[2].buf = (unsigned char *)buf;
	msgs[2].len = len + 1;
	msgs[2].scl_rate = MPU_I2C_RATE;
	//msgs[2].udelay = 200;

	res = i2c_transfer(i2c_adap, msgs, 3);
	if (res == 3)
		return 0;
	else if(res == 0)
		return -EBUSY;
	else
		return res;

}

int inv_serial_single_write(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned char register_addr,
	unsigned char data)
{
	return inv_i2c_write_register((struct i2c_adapter *)sl_handle,
				      slave_addr, register_addr, data);
}
EXPORT_SYMBOL(inv_serial_single_write);

int inv_serial_write(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short length,
	unsigned char const *data)
{
	int result;
	const unsigned short data_length = length - 1;
	const unsigned char start_reg_addr = data[0];
	unsigned char i2c_write[SERIAL_MAX_TRANSFER_SIZE + 1];
	unsigned short bytes_written = 0;

	while (bytes_written < data_length) {
		unsigned short this_len = min(SERIAL_MAX_TRANSFER_SIZE,
					     data_length - bytes_written);
		if (bytes_written == 0) {
			result = inv_i2c_write((struct i2c_adapter *)
					       sl_handle, slave_addr,
					       1 + this_len, data);
		} else {
			/* manually increment register addr between chunks */
			i2c_write[0] = start_reg_addr + bytes_written;
			memcpy(&i2c_write[1], &data[1 + bytes_written],
				this_len);
			result = inv_i2c_write((struct i2c_adapter *)
					       sl_handle, slave_addr,
					       1 + this_len, i2c_write);
		}
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		bytes_written += this_len;
	}
	return 0;
}
EXPORT_SYMBOL(inv_serial_write);

int inv_serial_read(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned char register_addr,
	unsigned short length,
	unsigned char *data)
{
	int result;
	unsigned short bytes_read = 0;

	if ((slave_addr & 0x7E) == DEFAULT_MPU_SLAVEADDR
		&& (register_addr == MPUREG_FIFO_R_W ||
		    register_addr == MPUREG_MEM_R_W)) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	while (bytes_read < length) {
		unsigned short this_len =
		    min(SERIAL_MAX_TRANSFER_SIZE, length - bytes_read);
		result = inv_i2c_read((struct i2c_adapter *)sl_handle,
				      slave_addr, register_addr + bytes_read,
				      this_len, &data[bytes_read]);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		bytes_read += this_len;
	}
	return 0;
}
EXPORT_SYMBOL(inv_serial_read);

int inv_serial_write_mem(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short mem_addr,
	unsigned short length,
	unsigned char const *data)
{
	int result;
	unsigned short bytes_written = 0;

	if ((mem_addr & 0xFF) + length > MPU_MEM_BANK_SIZE) {
		pr_err("memory read length (%d B) extends beyond its"
		       " limits (%d) if started at location %d\n", length,
		       MPU_MEM_BANK_SIZE, mem_addr & 0xFF);
		return INV_ERROR_INVALID_PARAMETER;
	}
	while (bytes_written < length) {
		unsigned short this_len =
		    min(SERIAL_MAX_TRANSFER_SIZE, length - bytes_written);
		result = mpu_memory_write((struct i2c_adapter *)sl_handle,
					  slave_addr, mem_addr + bytes_written,
					  this_len, &data[bytes_written]);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		bytes_written += this_len;
	}
	return 0;
}
EXPORT_SYMBOL(inv_serial_write_mem);

int inv_serial_read_mem(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short mem_addr,
	unsigned short length,
	unsigned char *data)
{
	int result;
	unsigned short bytes_read = 0;

	if ((mem_addr & 0xFF) + length > MPU_MEM_BANK_SIZE) {
		printk
		    ("memory read length (%d B) extends beyond its limits (%d) "
		     "if started at location %d\n", length,
		     MPU_MEM_BANK_SIZE, mem_addr & 0xFF);
		return INV_ERROR_INVALID_PARAMETER;
	}
	while (bytes_read < length) {
		unsigned short this_len =
		    min(SERIAL_MAX_TRANSFER_SIZE, length - bytes_read);
		result =
		    mpu_memory_read((struct i2c_adapter *)sl_handle,
				    slave_addr, mem_addr + bytes_read,
				    this_len, &data[bytes_read]);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		bytes_read += this_len;
	}
	return 0;
}
EXPORT_SYMBOL(inv_serial_read_mem);

int inv_serial_write_fifo(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short length,
	unsigned char const *data)
{
	int result;
	unsigned char i2c_write[SERIAL_MAX_TRANSFER_SIZE + 1];
	unsigned short bytes_written = 0;

	if (length > FIFO_HW_SIZE) {
		printk(KERN_ERR
		       "maximum fifo write length is %d\n", FIFO_HW_SIZE);
		return INV_ERROR_INVALID_PARAMETER;
	}
	while (bytes_written < length) {
		unsigned short this_len =
		    min(SERIAL_MAX_TRANSFER_SIZE, length - bytes_written);
		i2c_write[0] = MPUREG_FIFO_R_W;
		memcpy(&i2c_write[1], &data[bytes_written], this_len);
		result = inv_i2c_write((struct i2c_adapter *)sl_handle,
				       slave_addr, this_len + 1, i2c_write);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		bytes_written += this_len;
	}
	return 0;
}
EXPORT_SYMBOL(inv_serial_write_fifo);

int inv_serial_read_fifo(
	void *sl_handle,
	unsigned char slave_addr,
	unsigned short length,
	unsigned char *data)
{
	int result;
	unsigned short bytes_read = 0;

	if (length > FIFO_HW_SIZE) {
		printk(KERN_ERR
		       "maximum fifo read length is %d\n", FIFO_HW_SIZE);
		return INV_ERROR_INVALID_PARAMETER;
	}
	while (bytes_read < length) {
		unsigned short this_len =
		    min(SERIAL_MAX_TRANSFER_SIZE, length - bytes_read);
		result = inv_i2c_read((struct i2c_adapter *)sl_handle,
				      slave_addr, MPUREG_FIFO_R_W, this_len,
				      &data[bytes_read]);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		bytes_read += this_len;
	}

	return 0;
}
EXPORT_SYMBOL(inv_serial_read_fifo);

/**
 *  @}
 */
