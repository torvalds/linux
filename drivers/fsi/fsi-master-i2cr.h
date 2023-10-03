/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) IBM Corporation 2023 */

#ifndef DRIVERS_FSI_MASTER_I2CR_H
#define DRIVERS_FSI_MASTER_I2CR_H

#include <linux/i2c.h>
#include <linux/mutex.h>

#include "fsi-master.h"

struct i2c_client;

struct fsi_master_i2cr {
	struct fsi_master master;
	struct mutex lock;	/* protect HW access */
	struct i2c_client *client;
};

#define to_fsi_master_i2cr(m)	container_of(m, struct fsi_master_i2cr, master)

int fsi_master_i2cr_read(struct fsi_master_i2cr *i2cr, u32 addr, u64 *data);
int fsi_master_i2cr_write(struct fsi_master_i2cr *i2cr, u32 addr, u64 data);

static inline bool is_fsi_master_i2cr(struct fsi_master *master)
{
	if (master->dev.parent && master->dev.parent->type == &i2c_client_type)
		return true;

	return false;
}

#endif /* DRIVERS_FSI_MASTER_I2CR_H */
