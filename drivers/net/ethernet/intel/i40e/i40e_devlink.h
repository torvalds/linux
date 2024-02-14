/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023, Intel Corporation. */

#ifndef _I40E_DEVLINK_H_
#define _I40E_DEVLINK_H_

#include <linux/device.h>

struct i40e_pf;

struct i40e_pf *i40e_alloc_pf(struct device *dev);
void i40e_free_pf(struct i40e_pf *pf);
void i40e_devlink_register(struct i40e_pf *pf);
void i40e_devlink_unregister(struct i40e_pf *pf);
int i40e_devlink_create_port(struct i40e_pf *pf);
void i40e_devlink_destroy_port(struct i40e_pf *pf);

#endif /* _I40E_DEVLINK_H_ */
