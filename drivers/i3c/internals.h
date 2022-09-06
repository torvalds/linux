/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef I3C_INTERNALS_H
#define I3C_INTERNALS_H

#include <linux/i3c/master.h>
#include <linux/i3c/target.h>

extern struct bus_type i3c_bus_type;
extern const struct device_type i3c_masterdev_type;

void i3c_bus_normaluse_lock(struct i3c_bus *bus);
void i3c_bus_normaluse_unlock(struct i3c_bus *bus);

int i3c_dev_setdasa_locked(struct i3c_dev_desc *dev);
int i3c_dev_getstatus_locked(struct i3c_dev_desc *dev, struct i3c_device_info *info);
int i3c_dev_do_priv_xfers_locked(struct i3c_dev_desc *dev,
				 struct i3c_priv_xfer *xfers,
				 int nxfers);
int i3c_dev_disable_ibi_locked(struct i3c_dev_desc *dev);
int i3c_dev_enable_ibi_locked(struct i3c_dev_desc *dev);
int i3c_dev_request_ibi_locked(struct i3c_dev_desc *dev,
			       const struct i3c_ibi_setup *req);
void i3c_dev_free_ibi_locked(struct i3c_dev_desc *dev);
int i3c_dev_generate_ibi_locked(struct i3c_dev_desc *dev, const u8 *data, int len);
int i3c_dev_pending_read_notify_locked(struct i3c_dev_desc *dev,
				       struct i3c_priv_xfer *pending_read,
				       struct i3c_priv_xfer *ibi_notify);
int i3c_dev_is_ibi_enabled_locked(struct i3c_dev_desc *dev);
int i3c_for_each_dev(void *data, int (*fn)(struct device *, void *));
int i3c_dev_control_pec(struct i3c_dev_desc *dev, bool pec);
int i3c_master_getmrl_locked(struct i3c_master_controller *master,
			     struct i3c_device_info *info);
int i3c_master_getmwl_locked(struct i3c_master_controller *master,
			     struct i3c_device_info *info);
int i3c_master_setmrl_locked(struct i3c_master_controller *master,
			     struct i3c_device_info *info, u16 read_len, u8 ibi_len);
int i3c_master_setmwl_locked(struct i3c_master_controller *master,
			     struct i3c_device_info *info, u16 write_len);
#endif /* I3C_INTERNAL_H */
