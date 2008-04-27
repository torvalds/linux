/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005 Fen Systems Ltd.
 * Copyright 2006 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_I2C_DIRECT_H
#define EFX_I2C_DIRECT_H

#include "net_driver.h"

/*
 * Direct control of an I2C bus
 */

struct efx_i2c_interface;

/**
 * struct efx_i2c_bit_operations - I2C bus direct control methods
 *
 * I2C bus direct control methods.
 *
 * @setsda: Set state of SDA line
 * @setscl: Set state of SCL line
 * @getsda: Get state of SDA line
 * @getscl: Get state of SCL line
 * @udelay: Delay between each bit operation
 * @mdelay: Delay between each byte write
 */
struct efx_i2c_bit_operations {
	void (*setsda) (struct efx_i2c_interface *i2c);
	void (*setscl) (struct efx_i2c_interface *i2c);
	int (*getsda) (struct efx_i2c_interface *i2c);
	int (*getscl) (struct efx_i2c_interface *i2c);
	unsigned int udelay;
	unsigned int mdelay;
};

/**
 * struct efx_i2c_interface - an I2C interface
 *
 * An I2C interface.
 *
 * @efx: Attached Efx NIC
 * @op: I2C bus control methods
 * @sda: Current output state of SDA line
 * @scl: Current output state of SCL line
 */
struct efx_i2c_interface {
	struct efx_nic *efx;
	struct efx_i2c_bit_operations *op;
	unsigned int sda:1;
	unsigned int scl:1;
};

extern int efx_i2c_check_presence(struct efx_i2c_interface *i2c, u8 device_id);
extern int efx_i2c_fast_read(struct efx_i2c_interface *i2c,
			     u8 device_id, u8 offset,
			     u8 *data, unsigned int len);
extern int efx_i2c_fast_write(struct efx_i2c_interface *i2c,
			      u8 device_id, u8 offset,
			      const u8 *data, unsigned int len);
extern int efx_i2c_read(struct efx_i2c_interface *i2c,
			u8 device_id, u8 offset, u8 *data, unsigned int len);
extern int efx_i2c_write(struct efx_i2c_interface *i2c,
			 u8 device_id, u8 offset,
			 const u8 *data, unsigned int len);

extern int efx_i2c_send_bytes(struct efx_i2c_interface *i2c, u8 device_id,
			      const u8 *bytes, unsigned int len);

extern int efx_i2c_recv_bytes(struct efx_i2c_interface *i2c, u8 device_id,
			      u8 *bytes, unsigned int len);


/* Versions of the API that retry on failure. */
extern int efx_i2c_check_presence_retry(struct efx_i2c_interface *i2c,
					u8 device_id);

extern int efx_i2c_read_retry(struct efx_i2c_interface *i2c,
			u8 device_id, u8 offset, u8 *data, unsigned int len);

extern int efx_i2c_write_retry(struct efx_i2c_interface *i2c,
			 u8 device_id, u8 offset,
			 const u8 *data, unsigned int len);

#endif /* EFX_I2C_DIRECT_H */
