/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) IBM Corporation 2023 */

#ifndef DRIVERS_FSI_SLAVE_H
#define DRIVERS_FSI_SLAVE_H

#include <linux/cdev.h>
#include <linux/device.h>

struct fsi_master;

struct fsi_slave {
	struct device		dev;
	struct fsi_master	*master;
	struct cdev		cdev;
	int			cdev_idx;
	int			id;	/* FSI address */
	int			link;	/* FSI link# */
	u32			cfam_id;
	int			chip_id;
	uint32_t		size;	/* size of slave address space */
	u8			t_send_delay;
	u8			t_echo_delay;
};

#define to_fsi_slave(d) container_of(d, struct fsi_slave, dev)

#endif /* DRIVERS_FSI_SLAVE_H */
