/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005 Fen Systems Ltd.
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/delay.h>
#include "net_driver.h"
#include "i2c-direct.h"

/*
 * I2C data (SDA) and clock (SCL) line read/writes with appropriate
 * delays.
 */

static inline void setsda(struct efx_i2c_interface *i2c, int state)
{
	udelay(i2c->op->udelay);
	i2c->sda = state;
	i2c->op->setsda(i2c);
	udelay(i2c->op->udelay);
}

static inline void setscl(struct efx_i2c_interface *i2c, int state)
{
	udelay(i2c->op->udelay);
	i2c->scl = state;
	i2c->op->setscl(i2c);
	udelay(i2c->op->udelay);
}

static inline int getsda(struct efx_i2c_interface *i2c)
{
	int sda;

	udelay(i2c->op->udelay);
	sda = i2c->op->getsda(i2c);
	udelay(i2c->op->udelay);
	return sda;
}

static inline int getscl(struct efx_i2c_interface *i2c)
{
	int scl;

	udelay(i2c->op->udelay);
	scl = i2c->op->getscl(i2c);
	udelay(i2c->op->udelay);
	return scl;
}

/*
 * I2C low-level protocol operations
 *
 */

static inline void i2c_release(struct efx_i2c_interface *i2c)
{
	EFX_WARN_ON_PARANOID(!i2c->scl);
	EFX_WARN_ON_PARANOID(!i2c->sda);
	/* Devices may time out if operations do not end */
	setscl(i2c, 1);
	setsda(i2c, 1);
	EFX_BUG_ON_PARANOID(getsda(i2c) != 1);
	EFX_BUG_ON_PARANOID(getscl(i2c) != 1);
}

static inline void i2c_start(struct efx_i2c_interface *i2c)
{
	/* We may be restarting immediately after a {send,recv}_bit,
	 * so SCL will not necessarily already be high.
	 */
	EFX_WARN_ON_PARANOID(!i2c->sda);
	setscl(i2c, 1);
	setsda(i2c, 0);
	setscl(i2c, 0);
	setsda(i2c, 1);
}

static inline void i2c_send_bit(struct efx_i2c_interface *i2c, int bit)
{
	EFX_WARN_ON_PARANOID(i2c->scl != 0);
	setsda(i2c, bit);
	setscl(i2c, 1);
	setscl(i2c, 0);
	setsda(i2c, 1);
}

static inline int i2c_recv_bit(struct efx_i2c_interface *i2c)
{
	int bit;

	EFX_WARN_ON_PARANOID(i2c->scl != 0);
	EFX_WARN_ON_PARANOID(!i2c->sda);
	setscl(i2c, 1);
	bit = getsda(i2c);
	setscl(i2c, 0);
	return bit;
}

static inline void i2c_stop(struct efx_i2c_interface *i2c)
{
	EFX_WARN_ON_PARANOID(i2c->scl != 0);
	setsda(i2c, 0);
	setscl(i2c, 1);
	setsda(i2c, 1);
}

/*
 * I2C mid-level protocol operations
 *
 */

/* Sends a byte via the I2C bus and checks for an acknowledgement from
 * the slave device.
 */
static int i2c_send_byte(struct efx_i2c_interface *i2c, u8 byte)
{
	int i;

	/* Send byte */
	for (i = 0; i < 8; i++) {
		i2c_send_bit(i2c, !!(byte & 0x80));
		byte <<= 1;
	}

	/* Check for acknowledgement from slave */
	return (i2c_recv_bit(i2c) == 0 ? 0 : -EIO);
}

/* Receives a byte via the I2C bus and sends ACK/NACK to the slave device. */
static u8 i2c_recv_byte(struct efx_i2c_interface *i2c, int ack)
{
	u8 value = 0;
	int i;

	/* Receive byte */
	for (i = 0; i < 8; i++)
		value = (value << 1) | i2c_recv_bit(i2c);

	/* Send ACK/NACK */
	i2c_send_bit(i2c, (ack ? 0 : 1));

	return value;
}

/* Calculate command byte for a read operation */
static inline u8 i2c_read_cmd(u8 device_id)
{
	return ((device_id << 1) | 1);
}

/* Calculate command byte for a write operation */
static inline u8 i2c_write_cmd(u8 device_id)
{
	return ((device_id << 1) | 0);
}

int efx_i2c_check_presence(struct efx_i2c_interface *i2c, u8 device_id)
{
	int rc;

	/* If someone is driving the bus low we just give up. */
	if (getsda(i2c) == 0 || getscl(i2c) == 0) {
		EFX_ERR(i2c->efx, "%s someone is holding the I2C bus low."
			" Giving up.\n", __func__);
		return -EFAULT;
	}

	/* Pretend to initiate a device write */
	i2c_start(i2c);
	rc = i2c_send_byte(i2c, i2c_write_cmd(device_id));
	if (rc)
		goto out;

 out:
	i2c_stop(i2c);
	i2c_release(i2c);

	return rc;
}

/* This performs a fast read of one or more consecutive bytes from an
 * I2C device.  Not all devices support consecutive reads of more than
 * one byte; for these devices use efx_i2c_read() instead.
 */
int efx_i2c_fast_read(struct efx_i2c_interface *i2c,
		      u8 device_id, u8 offset, u8 *data, unsigned int len)
{
	int i;
	int rc;

	EFX_WARN_ON_PARANOID(getsda(i2c) != 1);
	EFX_WARN_ON_PARANOID(getscl(i2c) != 1);
	EFX_WARN_ON_PARANOID(data == NULL);
	EFX_WARN_ON_PARANOID(len < 1);

	/* Select device and starting offset */
	i2c_start(i2c);
	rc = i2c_send_byte(i2c, i2c_write_cmd(device_id));
	if (rc)
		goto out;
	rc = i2c_send_byte(i2c, offset);
	if (rc)
		goto out;

	/* Read data from device */
	i2c_start(i2c);
	rc = i2c_send_byte(i2c, i2c_read_cmd(device_id));
	if (rc)
		goto out;
	for (i = 0; i < (len - 1); i++)
		/* Read and acknowledge all but the last byte */
		data[i] = i2c_recv_byte(i2c, 1);
	/* Read last byte with no acknowledgement */
	data[i] = i2c_recv_byte(i2c, 0);

 out:
	i2c_stop(i2c);
	i2c_release(i2c);

	return rc;
}

/* This performs a fast write of one or more consecutive bytes to an
 * I2C device.  Not all devices support consecutive writes of more
 * than one byte; for these devices use efx_i2c_write() instead.
 */
int efx_i2c_fast_write(struct efx_i2c_interface *i2c,
		       u8 device_id, u8 offset,
		       const u8 *data, unsigned int len)
{
	int i;
	int rc;

	EFX_WARN_ON_PARANOID(getsda(i2c) != 1);
	EFX_WARN_ON_PARANOID(getscl(i2c) != 1);
	EFX_WARN_ON_PARANOID(len < 1);

	/* Select device and starting offset */
	i2c_start(i2c);
	rc = i2c_send_byte(i2c, i2c_write_cmd(device_id));
	if (rc)
		goto out;
	rc = i2c_send_byte(i2c, offset);
	if (rc)
		goto out;

	/* Write data to device */
	for (i = 0; i < len; i++) {
		rc = i2c_send_byte(i2c, data[i]);
		if (rc)
			goto out;
	}

 out:
	i2c_stop(i2c);
	i2c_release(i2c);

	return rc;
}

/* I2C byte-by-byte read */
int efx_i2c_read(struct efx_i2c_interface *i2c,
		 u8 device_id, u8 offset, u8 *data, unsigned int len)
{
	int rc;

	/* i2c_fast_read with length 1 is a single byte read */
	for (; len > 0; offset++, data++, len--) {
		rc = efx_i2c_fast_read(i2c, device_id, offset, data, 1);
		if (rc)
			return rc;
	}

	return 0;
}

/* I2C byte-by-byte write */
int efx_i2c_write(struct efx_i2c_interface *i2c,
		  u8 device_id, u8 offset, const u8 *data, unsigned int len)
{
	int rc;

	/* i2c_fast_write with length 1 is a single byte write */
	for (; len > 0; offset++, data++, len--) {
		rc = efx_i2c_fast_write(i2c, device_id, offset, data, 1);
		if (rc)
			return rc;
		mdelay(i2c->op->mdelay);
	}

	return 0;
}


/* This is just a slightly neater wrapper round efx_i2c_fast_write
 * in the case where the target doesn't take an offset
 */
int efx_i2c_send_bytes(struct efx_i2c_interface *i2c,
		       u8 device_id, const u8 *data, unsigned int len)
{
	return efx_i2c_fast_write(i2c, device_id, data[0], data + 1, len - 1);
}

/* I2C receiving of bytes - does not send an offset byte */
int efx_i2c_recv_bytes(struct efx_i2c_interface *i2c, u8 device_id,
		       u8 *bytes, unsigned int len)
{
	int i;
	int rc;

	EFX_WARN_ON_PARANOID(getsda(i2c) != 1);
	EFX_WARN_ON_PARANOID(getscl(i2c) != 1);
	EFX_WARN_ON_PARANOID(len < 1);

	/* Select device */
	i2c_start(i2c);

	/* Read data from device */
	rc = i2c_send_byte(i2c, i2c_read_cmd(device_id));
	if (rc)
		goto out;

	for (i = 0; i < (len - 1); i++)
		/* Read and acknowledge all but the last byte */
		bytes[i] = i2c_recv_byte(i2c, 1);
	/* Read last byte with no acknowledgement */
	bytes[i] = i2c_recv_byte(i2c, 0);

 out:
	i2c_stop(i2c);
	i2c_release(i2c);

	return rc;
}

/* SMBus and some I2C devices will time out if the I2C clock is
 * held low for too long. This is most likely to happen in virtualised
 * systems (when the entire domain is descheduled) but could in
 * principle happen due to preemption on any busy system (and given the
 * potential length of an I2C operation turning preemption off is not
 * a sensible option). The following functions deal with the failure by
 * retrying up to a fixed number of times.
  */

#define I2C_MAX_RETRIES	(10)

/* The timeout problem will result in -EIO. If the wrapped function
 * returns any other error, pass this up and do not retry. */
#define RETRY_WRAPPER(_f) \
	int retries = I2C_MAX_RETRIES; \
	int rc; \
	while (retries) { \
		rc = _f; \
		if (rc != -EIO) \
			return rc; \
		retries--; \
	} \
	return rc; \

int efx_i2c_check_presence_retry(struct efx_i2c_interface *i2c, u8 device_id)
{
	RETRY_WRAPPER(efx_i2c_check_presence(i2c, device_id))
}

int efx_i2c_read_retry(struct efx_i2c_interface *i2c,
		 u8 device_id, u8 offset, u8 *data, unsigned int len)
{
	RETRY_WRAPPER(efx_i2c_read(i2c, device_id, offset, data, len))
}

int efx_i2c_write_retry(struct efx_i2c_interface *i2c,
		  u8 device_id, u8 offset, const u8 *data, unsigned int len)
{
	RETRY_WRAPPER(efx_i2c_write(i2c, device_id, offset, data, len))
}
