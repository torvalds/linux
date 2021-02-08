// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 * Mellanox i2c mux driver
 *
 * Copyright (C) 2016-2020 Mellanox Technologies
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_data/mlxcpld.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define CPLD_MUX_MAX_NCHANS	8

/* mlxcpld_mux - mux control structure:
 * @last_val - last selected register value or -1 if mux deselected
 * @client - I2C device client
 * @pdata: platform data
 */
struct mlxcpld_mux {
	int last_val;
	struct i2c_client *client;
	struct mlxcpld_mux_plat_data pdata;
};

/* MUX logic description.
 * Driver can support different mux control logic, according to CPLD
 * implementation.
 *
 * Connectivity schema.
 *
 * i2c-mlxcpld                                 Digital               Analog
 * driver
 * *--------*                                 * -> mux1 (virt bus2) -> mux -> |
 * | I2CLPC | i2c physical                    * -> mux2 (virt bus3) -> mux -> |
 * | bridge | bus 1                 *---------*                               |
 * | logic  |---------------------> * mux reg *                               |
 * | in CPLD|                       *---------*                               |
 * *--------*   i2c-mux-mlxpcld          ^    * -> muxn (virt busn) -> mux -> |
 *     |        driver                   |                                    |
 *     |        *---------------*        |                              Devices
 *     |        * CPLD (i2c bus)* select |
 *     |        * registers for *--------*
 *     |        * mux selection * deselect
 *     |        *---------------*
 *     |                 |
 * <-------->     <----------->
 * i2c cntrl      Board cntrl reg
 * reg space      space (mux select,
 *                IO, LED, WD, info)
 *
 */

/* Write to mux register. Don't use i2c_transfer() and i2c_smbus_xfer()
 * for this as they will try to lock adapter a second time.
 */
static int mlxcpld_mux_reg_write(struct i2c_adapter *adap,
				 struct mlxcpld_mux *mux, u32 val)
{
	struct i2c_client *client = mux->client;
	union i2c_smbus_data data = { .byte = val };

	return __i2c_smbus_xfer(adap, client->addr, client->flags,
				I2C_SMBUS_WRITE, mux->pdata.sel_reg_addr,
				I2C_SMBUS_BYTE_DATA, &data);
}

static int mlxcpld_mux_select_chan(struct i2c_mux_core *muxc, u32 chan)
{
	struct mlxcpld_mux *mux = i2c_mux_priv(muxc);
	u32 regval = chan + 1;
	int err = 0;

	/* Only select the channel if its different from the last channel */
	if (mux->last_val != regval) {
		err = mlxcpld_mux_reg_write(muxc->parent, mux, regval);
		mux->last_val = err < 0 ? -1 : regval;
	}

	return err;
}

static int mlxcpld_mux_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	struct mlxcpld_mux *mux = i2c_mux_priv(muxc);

	/* Deselect active channel */
	mux->last_val = -1;

	return mlxcpld_mux_reg_write(muxc->parent, mux, 0);
}

/* Probe/reomove functions */
static int mlxcpld_mux_probe(struct platform_device *pdev)
{
	struct mlxcpld_mux_plat_data *pdata = dev_get_platdata(&pdev->dev);
	struct i2c_client *client = to_i2c_client(pdev->dev.parent);
	struct i2c_mux_core *muxc;
	int num, force;
	struct mlxcpld_mux *data;
	int err;

	if (!pdata)
		return -EINVAL;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	muxc = i2c_mux_alloc(client->adapter, &pdev->dev, CPLD_MUX_MAX_NCHANS,
			     sizeof(*data), 0, mlxcpld_mux_select_chan,
			     mlxcpld_mux_deselect);
	if (!muxc)
		return -ENOMEM;

	platform_set_drvdata(pdev, muxc);
	data = i2c_mux_priv(muxc);
	data->client = client;
	memcpy(&data->pdata, pdata, sizeof(*pdata));
	data->last_val = -1; /* force the first selection */

	/* Create an adapter for each channel. */
	for (num = 0; num < CPLD_MUX_MAX_NCHANS; num++) {
		if (num >= pdata->num_adaps)
			/* discard unconfigured channels */
			break;

		force = pdata->adap_ids[num];

		err = i2c_mux_add_adapter(muxc, force, num, 0);
		if (err)
			goto virt_reg_failed;
	}

	return 0;

virt_reg_failed:
	i2c_mux_del_adapters(muxc);
	return err;
}

static int mlxcpld_mux_remove(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc = platform_get_drvdata(pdev);

	i2c_mux_del_adapters(muxc);
	return 0;
}

static struct platform_driver mlxcpld_mux_driver = {
	.driver = {
		.name = "i2c-mux-mlxcpld",
	},
	.probe = mlxcpld_mux_probe,
	.remove = mlxcpld_mux_remove,
};

module_platform_driver(mlxcpld_mux_driver);

MODULE_AUTHOR("Michael Shych (michaels@mellanox.com)");
MODULE_DESCRIPTION("Mellanox I2C-CPLD-MUX driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:i2c-mux-mlxcpld");
