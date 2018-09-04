// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2016-2018 Mellanox Technologies. All rights reserved */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/types.h>

#include "core.h"
#include "i2c.h"

static const char mlxsw_minimal_driver_name[] = "mlxsw_minimal";

static const struct mlxsw_config_profile mlxsw_minimal_config_profile;

static struct mlxsw_driver mlxsw_minimal_driver = {
	.kind		= mlxsw_minimal_driver_name,
	.priv_size	= 1,
	.profile	= &mlxsw_minimal_config_profile,
};

static const struct i2c_device_id mlxsw_minimal_i2c_id[] = {
	{ "mlxsw_minimal", 0},
	{ },
};

static struct i2c_driver mlxsw_minimal_i2c_driver = {
	.driver.name = "mlxsw_minimal",
	.class = I2C_CLASS_HWMON,
	.id_table = mlxsw_minimal_i2c_id,
};

static int __init mlxsw_minimal_module_init(void)
{
	int err;

	err = mlxsw_core_driver_register(&mlxsw_minimal_driver);
	if (err)
		return err;

	err = mlxsw_i2c_driver_register(&mlxsw_minimal_i2c_driver);
	if (err)
		goto err_i2c_driver_register;

	return 0;

err_i2c_driver_register:
	mlxsw_core_driver_unregister(&mlxsw_minimal_driver);

	return err;
}

static void __exit mlxsw_minimal_module_exit(void)
{
	mlxsw_i2c_driver_unregister(&mlxsw_minimal_i2c_driver);
	mlxsw_core_driver_unregister(&mlxsw_minimal_driver);
}

module_init(mlxsw_minimal_module_init);
module_exit(mlxsw_minimal_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("Mellanox minimal driver");
MODULE_DEVICE_TABLE(i2c, mlxsw_minimal_i2c_id);
