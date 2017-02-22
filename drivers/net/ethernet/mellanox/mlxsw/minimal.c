/*
 * drivers/net/ethernet/mellanox/mlxsw/minimal.c
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Vadim Pasternak <vadimp@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
