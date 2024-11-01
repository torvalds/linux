/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __I2C_CCGX_UCSI_H_
#define __I2C_CCGX_UCSI_H_

struct i2c_adapter;
struct i2c_client;
struct software_node;

struct i2c_client *i2c_new_ccgx_ucsi(struct i2c_adapter *adapter, int irq,
				     const struct software_node *swnode);
#endif /* __I2C_CCGX_UCSI_H_ */
