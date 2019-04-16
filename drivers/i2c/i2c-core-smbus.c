/*
 * Linux I2C core SMBus and SMBus emulation code
 *
 * This file contains the SMBus functions which are always included in the I2C
 * core because they can be emulated via I2C. SMBus specific extensions
 * (e.g. smbalert) are handled in a seperate i2c-smbus module.
 *
 * All SMBus-related things are written by Frodo Looijaard <frodol@dds.nl>
 * SMBus 2.0 support by Mark Studebaker <mdsxyz123@yahoo.com> and
 * Jean Delvare <jdelvare@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/slab.h>

#define CREATE_TRACE_POINTS
#include <trace/events/smbus.h>


/* The SMBus parts */

#define POLY    (0x1070U << 3)
static u8 crc8(u16 data)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (data & 0x8000)
			data = data ^ POLY;
		data = data << 1;
	}
	return (u8)(data >> 8);
}

/* Incremental CRC8 over count bytes in the array pointed to by p */
static u8 i2c_smbus_pec(u8 crc, u8 *p, size_t count)
{
	int i;

	for (i = 0; i < count; i++)
		crc = crc8((crc ^ p[i]) << 8);
	return crc;
}

/* Assume a 7-bit address, which is reasonable for SMBus */
static u8 i2c_smbus_msg_pec(u8 pec, struct i2c_msg *msg)
{
	/* The address will be sent first */
	u8 addr = i2c_8bit_addr_from_msg(msg);
	pec = i2c_smbus_pec(pec, &addr, 1);

	/* The data buffer follows */
	return i2c_smbus_pec(pec, msg->buf, msg->len);
}

/* Used for write only transactions */
static inline void i2c_smbus_add_pec(struct i2c_msg *msg)
{
	msg->buf[msg->len] = i2c_smbus_msg_pec(0, msg);
	msg->len++;
}

/* Return <0 on CRC error
   If there was a write before this read (most cases) we need to take the
   partial CRC from the write part into account.
   Note that this function does modify the message (we need to decrease the
   message length to hide the CRC byte from the caller). */
static int i2c_smbus_check_pec(u8 cpec, struct i2c_msg *msg)
{
	u8 rpec = msg->buf[--msg->len];
	cpec = i2c_smbus_msg_pec(cpec, msg);

	if (rpec != cpec) {
		pr_debug("Bad PEC 0x%02x vs. 0x%02x\n",
			rpec, cpec);
		return -EBADMSG;
	}
	return 0;
}

/**
 * i2c_smbus_read_byte - SMBus "receive byte" protocol
 * @client: Handle to slave device
 *
 * This executes the SMBus "receive byte" protocol, returning negative errno
 * else the byte received from the device.
 */
s32 i2c_smbus_read_byte(const struct i2c_client *client)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, 0,
				I2C_SMBUS_BYTE, &data);
	return (status < 0) ? status : data.byte;
}
EXPORT_SYMBOL(i2c_smbus_read_byte);

/**
 * i2c_smbus_write_byte - SMBus "send byte" protocol
 * @client: Handle to slave device
 * @value: Byte to be sent
 *
 * This executes the SMBus "send byte" protocol, returning negative errno
 * else zero on success.
 */
s32 i2c_smbus_write_byte(const struct i2c_client *client, u8 value)
{
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
	                      I2C_SMBUS_WRITE, value, I2C_SMBUS_BYTE, NULL);
}
EXPORT_SYMBOL(i2c_smbus_write_byte);

/**
 * i2c_smbus_read_byte_data - SMBus "read byte" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 *
 * This executes the SMBus "read byte" protocol, returning negative errno
 * else a data byte received from the device.
 */
s32 i2c_smbus_read_byte_data(const struct i2c_client *client, u8 command)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, command,
				I2C_SMBUS_BYTE_DATA, &data);
	return (status < 0) ? status : data.byte;
}
EXPORT_SYMBOL(i2c_smbus_read_byte_data);

/**
 * i2c_smbus_write_byte_data - SMBus "write byte" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @value: Byte being written
 *
 * This executes the SMBus "write byte" protocol, returning negative errno
 * else zero on success.
 */
s32 i2c_smbus_write_byte_data(const struct i2c_client *client, u8 command,
			      u8 value)
{
	union i2c_smbus_data data;
	data.byte = value;
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_BYTE_DATA, &data);
}
EXPORT_SYMBOL(i2c_smbus_write_byte_data);

/**
 * i2c_smbus_read_word_data - SMBus "read word" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 *
 * This executes the SMBus "read word" protocol, returning negative errno
 * else a 16-bit unsigned "word" received from the device.
 */
s32 i2c_smbus_read_word_data(const struct i2c_client *client, u8 command)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, command,
				I2C_SMBUS_WORD_DATA, &data);
	return (status < 0) ? status : data.word;
}
EXPORT_SYMBOL(i2c_smbus_read_word_data);

/**
 * i2c_smbus_write_word_data - SMBus "write word" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @value: 16-bit "word" being written
 *
 * This executes the SMBus "write word" protocol, returning negative errno
 * else zero on success.
 */
s32 i2c_smbus_write_word_data(const struct i2c_client *client, u8 command,
			      u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_WORD_DATA, &data);
}
EXPORT_SYMBOL(i2c_smbus_write_word_data);

/**
 * i2c_smbus_read_block_data - SMBus "block read" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @values: Byte array into which data will be read; big enough to hold
 *	the data returned by the slave.  SMBus allows at most 32 bytes.
 *
 * This executes the SMBus "block read" protocol, returning negative errno
 * else the number of data bytes in the slave's response.
 *
 * Note that using this function requires that the client's adapter support
 * the I2C_FUNC_SMBUS_READ_BLOCK_DATA functionality.  Not all adapter drivers
 * support this; its emulation through I2C messaging relies on a specific
 * mechanism (I2C_M_RECV_LEN) which may not be implemented.
 */
s32 i2c_smbus_read_block_data(const struct i2c_client *client, u8 command,
			      u8 *values)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, command,
				I2C_SMBUS_BLOCK_DATA, &data);
	if (status)
		return status;

	memcpy(values, &data.block[1], data.block[0]);
	return data.block[0];
}
EXPORT_SYMBOL(i2c_smbus_read_block_data);

/**
 * i2c_smbus_write_block_data - SMBus "block write" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @length: Size of data block; SMBus allows at most 32 bytes
 * @values: Byte array which will be written.
 *
 * This executes the SMBus "block write" protocol, returning negative errno
 * else zero on success.
 */
s32 i2c_smbus_write_block_data(const struct i2c_client *client, u8 command,
			       u8 length, const u8 *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	memcpy(&data.block[1], values, length);
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_BLOCK_DATA, &data);
}
EXPORT_SYMBOL(i2c_smbus_write_block_data);

/* Returns the number of read bytes */
s32 i2c_smbus_read_i2c_block_data(const struct i2c_client *client, u8 command,
				  u8 length, u8 *values)
{
	union i2c_smbus_data data;
	int status;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
				I2C_SMBUS_READ, command,
				I2C_SMBUS_I2C_BLOCK_DATA, &data);
	if (status < 0)
		return status;

	memcpy(values, &data.block[1], data.block[0]);
	return data.block[0];
}
EXPORT_SYMBOL(i2c_smbus_read_i2c_block_data);

s32 i2c_smbus_write_i2c_block_data(const struct i2c_client *client, u8 command,
				   u8 length, const u8 *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	memcpy(data.block + 1, values, length);
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_I2C_BLOCK_DATA, &data);
}
EXPORT_SYMBOL(i2c_smbus_write_i2c_block_data);

static void i2c_smbus_try_get_dmabuf(struct i2c_msg *msg, u8 init_val)
{
	bool is_read = msg->flags & I2C_M_RD;
	unsigned char *dma_buf;

	dma_buf = kzalloc(I2C_SMBUS_BLOCK_MAX + (is_read ? 2 : 3), GFP_KERNEL);
	if (!dma_buf)
		return;

	msg->buf = dma_buf;
	msg->flags |= I2C_M_DMA_SAFE;

	if (init_val)
		msg->buf[0] = init_val;
}

/*
 * Simulate a SMBus command using the I2C protocol.
 * No checking of parameters is done!
 */
static s32 i2c_smbus_xfer_emulated(struct i2c_adapter *adapter, u16 addr,
				   unsigned short flags,
				   char read_write, u8 command, int size,
				   union i2c_smbus_data *data)
{
	/*
	 * So we need to generate a series of msgs. In the case of writing, we
	 * need to use only one message; when reading, we need two. We
	 * initialize most things with sane defaults, to keep the code below
	 * somewhat simpler.
	 */
	unsigned char msgbuf0[I2C_SMBUS_BLOCK_MAX+3];
	unsigned char msgbuf1[I2C_SMBUS_BLOCK_MAX+2];
	int num = read_write == I2C_SMBUS_READ ? 2 : 1;
	int i;
	u8 partial_pec = 0;
	int status;
	struct i2c_msg msg[2] = {
		{
			.addr = addr,
			.flags = flags,
			.len = 1,
			.buf = msgbuf0,
		}, {
			.addr = addr,
			.flags = flags | I2C_M_RD,
			.len = 0,
			.buf = msgbuf1,
		},
	};

	msgbuf0[0] = command;
	switch (size) {
	case I2C_SMBUS_QUICK:
		msg[0].len = 0;
		/* Special case: The read/write field is used as data */
		msg[0].flags = flags | (read_write == I2C_SMBUS_READ ?
					I2C_M_RD : 0);
		num = 1;
		break;
	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_READ) {
			/* Special case: only a read! */
			msg[0].flags = I2C_M_RD | flags;
			num = 1;
		}
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 1;
		else {
			msg[0].len = 2;
			msgbuf0[1] = data->byte;
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 2;
		else {
			msg[0].len = 3;
			msgbuf0[1] = data->word & 0xff;
			msgbuf0[2] = data->word >> 8;
		}
		break;
	case I2C_SMBUS_PROC_CALL:
		num = 2; /* Special case */
		read_write = I2C_SMBUS_READ;
		msg[0].len = 3;
		msg[1].len = 2;
		msgbuf0[1] = data->word & 0xff;
		msgbuf0[2] = data->word >> 8;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			msg[1].flags |= I2C_M_RECV_LEN;
			msg[1].len = 1; /* block length will be added by
					   the underlying bus driver */
			i2c_smbus_try_get_dmabuf(&msg[1], 0);
		} else {
			msg[0].len = data->block[0] + 2;
			if (msg[0].len > I2C_SMBUS_BLOCK_MAX + 2) {
				dev_err(&adapter->dev,
					"Invalid block write size %d\n",
					data->block[0]);
				return -EINVAL;
			}

			i2c_smbus_try_get_dmabuf(&msg[0], command);
			for (i = 1; i < msg[0].len; i++)
				msg[0].buf[i] = data->block[i - 1];
		}
		break;
	case I2C_SMBUS_BLOCK_PROC_CALL:
		num = 2; /* Another special case */
		read_write = I2C_SMBUS_READ;
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX) {
			dev_err(&adapter->dev,
				"Invalid block write size %d\n",
				data->block[0]);
			return -EINVAL;
		}

		msg[0].len = data->block[0] + 2;
		i2c_smbus_try_get_dmabuf(&msg[0], command);
		for (i = 1; i < msg[0].len; i++)
			msg[0].buf[i] = data->block[i - 1];

		msg[1].flags |= I2C_M_RECV_LEN;
		msg[1].len = 1; /* block length will be added by
				   the underlying bus driver */
		i2c_smbus_try_get_dmabuf(&msg[1], 0);
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX) {
			dev_err(&adapter->dev, "Invalid block %s size %d\n",
				read_write == I2C_SMBUS_READ ? "read" : "write",
				data->block[0]);
			return -EINVAL;
		}

		if (read_write == I2C_SMBUS_READ) {
			msg[1].len = data->block[0];
			i2c_smbus_try_get_dmabuf(&msg[1], 0);
		} else {
			msg[0].len = data->block[0] + 1;

			i2c_smbus_try_get_dmabuf(&msg[0], command);
			for (i = 1; i <= data->block[0]; i++)
				msg[0].buf[i] = data->block[i];
		}
		break;
	default:
		dev_err(&adapter->dev, "Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}

	i = ((flags & I2C_CLIENT_PEC) && size != I2C_SMBUS_QUICK
				      && size != I2C_SMBUS_I2C_BLOCK_DATA);
	if (i) {
		/* Compute PEC if first message is a write */
		if (!(msg[0].flags & I2C_M_RD)) {
			if (num == 1) /* Write only */
				i2c_smbus_add_pec(&msg[0]);
			else /* Write followed by read */
				partial_pec = i2c_smbus_msg_pec(0, &msg[0]);
		}
		/* Ask for PEC if last message is a read */
		if (msg[num-1].flags & I2C_M_RD)
			msg[num-1].len++;
	}

	status = __i2c_transfer(adapter, msg, num);
	if (status < 0)
		goto cleanup;
	if (status != num) {
		status = -EIO;
		goto cleanup;
	}
	status = 0;

	/* Check PEC if last message is a read */
	if (i && (msg[num-1].flags & I2C_M_RD)) {
		status = i2c_smbus_check_pec(partial_pec, &msg[num-1]);
		if (status < 0)
			goto cleanup;
	}

	if (read_write == I2C_SMBUS_READ)
		switch (size) {
		case I2C_SMBUS_BYTE:
			data->byte = msgbuf0[0];
			break;
		case I2C_SMBUS_BYTE_DATA:
			data->byte = msgbuf1[0];
			break;
		case I2C_SMBUS_WORD_DATA:
		case I2C_SMBUS_PROC_CALL:
			data->word = msgbuf1[0] | (msgbuf1[1] << 8);
			break;
		case I2C_SMBUS_I2C_BLOCK_DATA:
			for (i = 0; i < data->block[0]; i++)
				data->block[i + 1] = msg[1].buf[i];
			break;
		case I2C_SMBUS_BLOCK_DATA:
		case I2C_SMBUS_BLOCK_PROC_CALL:
			for (i = 0; i < msg[1].buf[0] + 1; i++)
				data->block[i] = msg[1].buf[i];
			break;
		}

cleanup:
	if (msg[0].flags & I2C_M_DMA_SAFE)
		kfree(msg[0].buf);
	if (msg[1].flags & I2C_M_DMA_SAFE)
		kfree(msg[1].buf);

	return status;
}

/**
 * i2c_smbus_xfer - execute SMBus protocol operations
 * @adapter: Handle to I2C bus
 * @addr: Address of SMBus slave on that bus
 * @flags: I2C_CLIENT_* flags (usually zero or I2C_CLIENT_PEC)
 * @read_write: I2C_SMBUS_READ or I2C_SMBUS_WRITE
 * @command: Byte interpreted by slave, for protocols which use such bytes
 * @protocol: SMBus protocol operation to execute, such as I2C_SMBUS_PROC_CALL
 * @data: Data to be read or written
 *
 * This executes an SMBus protocol operation, and returns a negative
 * errno code else zero on success.
 */
s32 i2c_smbus_xfer(struct i2c_adapter *adapter, u16 addr,
		   unsigned short flags, char read_write,
		   u8 command, int protocol, union i2c_smbus_data *data)
{
	s32 res;

	i2c_lock_bus(adapter, I2C_LOCK_SEGMENT);
	res = __i2c_smbus_xfer(adapter, addr, flags, read_write,
			       command, protocol, data);
	i2c_unlock_bus(adapter, I2C_LOCK_SEGMENT);

	return res;
}
EXPORT_SYMBOL(i2c_smbus_xfer);

s32 __i2c_smbus_xfer(struct i2c_adapter *adapter, u16 addr,
		     unsigned short flags, char read_write,
		     u8 command, int protocol, union i2c_smbus_data *data)
{
	unsigned long orig_jiffies;
	int try;
	s32 res;

	/* If enabled, the following two tracepoints are conditional on
	 * read_write and protocol.
	 */
	trace_smbus_write(adapter, addr, flags, read_write,
			  command, protocol, data);
	trace_smbus_read(adapter, addr, flags, read_write,
			 command, protocol);

	flags &= I2C_M_TEN | I2C_CLIENT_PEC | I2C_CLIENT_SCCB;

	if (adapter->algo->smbus_xfer) {
		/* Retry automatically on arbitration loss */
		orig_jiffies = jiffies;
		for (res = 0, try = 0; try <= adapter->retries; try++) {
			res = adapter->algo->smbus_xfer(adapter, addr, flags,
							read_write, command,
							protocol, data);
			if (res != -EAGAIN)
				break;
			if (time_after(jiffies,
				       orig_jiffies + adapter->timeout))
				break;
		}

		if (res != -EOPNOTSUPP || !adapter->algo->master_xfer)
			goto trace;
		/*
		 * Fall back to i2c_smbus_xfer_emulated if the adapter doesn't
		 * implement native support for the SMBus operation.
		 */
	}

	res = i2c_smbus_xfer_emulated(adapter, addr, flags, read_write,
				      command, protocol, data);

trace:
	/* If enabled, the reply tracepoint is conditional on read_write. */
	trace_smbus_reply(adapter, addr, flags, read_write,
			  command, protocol, data, res);
	trace_smbus_result(adapter, addr, flags, read_write,
			   command, protocol, res);

	return res;
}
EXPORT_SYMBOL(__i2c_smbus_xfer);

/**
 * i2c_smbus_read_i2c_block_data_or_emulated - read block or emulate
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @length: Size of data block; SMBus allows at most I2C_SMBUS_BLOCK_MAX bytes
 * @values: Byte array into which data will be read; big enough to hold
 *	the data returned by the slave.  SMBus allows at most
 *	I2C_SMBUS_BLOCK_MAX bytes.
 *
 * This executes the SMBus "block read" protocol if supported by the adapter.
 * If block read is not supported, it emulates it using either word or byte
 * read protocols depending on availability.
 *
 * The addresses of the I2C slave device that are accessed with this function
 * must be mapped to a linear region, so that a block read will have the same
 * effect as a byte read. Before using this function you must double-check
 * if the I2C slave does support exchanging a block transfer with a byte
 * transfer.
 */
s32 i2c_smbus_read_i2c_block_data_or_emulated(const struct i2c_client *client,
					      u8 command, u8 length, u8 *values)
{
	u8 i = 0;
	int status;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;

	if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return i2c_smbus_read_i2c_block_data(client, command, length, values);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return -EOPNOTSUPP;

	if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_WORD_DATA)) {
		while ((i + 2) <= length) {
			status = i2c_smbus_read_word_data(client, command + i);
			if (status < 0)
				return status;
			values[i] = status & 0xff;
			values[i + 1] = status >> 8;
			i += 2;
		}
	}

	while (i < length) {
		status = i2c_smbus_read_byte_data(client, command + i);
		if (status < 0)
			return status;
		values[i] = status;
		i++;
	}

	return i;
}
EXPORT_SYMBOL(i2c_smbus_read_i2c_block_data_or_emulated);

/**
 * i2c_setup_smbus_alert - Setup SMBus alert support
 * @adapter: the target adapter
 * @setup: setup data for the SMBus alert handler
 * Context: can sleep
 *
 * Setup handling of the SMBus alert protocol on a given I2C bus segment.
 *
 * Handling can be done either through our IRQ handler, or by the
 * adapter (from its handler, periodic polling, or whatever).
 *
 * NOTE that if we manage the IRQ, we *MUST* know if it's level or
 * edge triggered in order to hand it to the workqueue correctly.
 * If triggering the alert seems to wedge the system, you probably
 * should have said it's level triggered.
 *
 * This returns the ara client, which should be saved for later use with
 * i2c_handle_smbus_alert() and ultimately i2c_unregister_device(); or NULL
 * to indicate an error.
 */
struct i2c_client *i2c_setup_smbus_alert(struct i2c_adapter *adapter,
					 struct i2c_smbus_alert_setup *setup)
{
	struct i2c_board_info ara_board_info = {
		I2C_BOARD_INFO("smbus_alert", 0x0c),
		.platform_data = setup,
	};

	return i2c_new_device(adapter, &ara_board_info);
}
EXPORT_SYMBOL_GPL(i2c_setup_smbus_alert);

#if IS_ENABLED(CONFIG_I2C_SMBUS) && IS_ENABLED(CONFIG_OF)
int of_i2c_setup_smbus_alert(struct i2c_adapter *adapter)
{
	struct i2c_client *client;
	int irq;

	irq = of_property_match_string(adapter->dev.of_node, "interrupt-names",
				       "smbus_alert");
	if (irq == -EINVAL || irq == -ENODATA)
		return 0;
	else if (irq < 0)
		return irq;

	client = i2c_setup_smbus_alert(adapter, NULL);
	if (!client)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL_GPL(of_i2c_setup_smbus_alert);
#endif
