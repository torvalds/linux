/*
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

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/i2c-mux-reg.h>
#include <linux/platform_data/mlxcpld-hotplug.h>

#define MLX_PLAT_DEVICE_NAME		"mlxplat"

/* LPC bus IO offsets */
#define MLXPLAT_CPLD_LPC_I2C_BASE_ADRR		0x2000
#define MLXPLAT_CPLD_LPC_REG_BASE_ADRR		0x2500
#define MLXPLAT_CPLD_LPC_REG_AGGR_ADRR		0x253a
#define MLXPLAT_CPLD_LPC_REG_PSU_ADRR		0x2558
#define MLXPLAT_CPLD_LPC_REG_PWR_ADRR		0x2564
#define MLXPLAT_CPLD_LPC_REG_FAN_ADRR		0x2588
#define MLXPLAT_CPLD_LPC_IO_RANGE		0x100
#define MLXPLAT_CPLD_LPC_I2C_CH1_OFF		0xdb
#define MLXPLAT_CPLD_LPC_I2C_CH2_OFF		0xda
#define MLXPLAT_CPLD_LPC_PIO_OFFSET		0x10000UL
#define MLXPLAT_CPLD_LPC_REG1	((MLXPLAT_CPLD_LPC_REG_BASE_ADRR + \
				  MLXPLAT_CPLD_LPC_I2C_CH1_OFF) | \
				  MLXPLAT_CPLD_LPC_PIO_OFFSET)
#define MLXPLAT_CPLD_LPC_REG2	((MLXPLAT_CPLD_LPC_REG_BASE_ADRR + \
				  MLXPLAT_CPLD_LPC_I2C_CH2_OFF) | \
				  MLXPLAT_CPLD_LPC_PIO_OFFSET)

/* Masks for aggregation, psu, pwr and fan event in CPLD related registers. */
#define MLXPLAT_CPLD_AGGR_PSU_MASK_DEF	0x08
#define MLXPLAT_CPLD_AGGR_PWR_MASK_DEF	0x08
#define MLXPLAT_CPLD_AGGR_FAN_MASK_DEF	0x40
#define MLXPLAT_CPLD_AGGR_MASK_DEF	(MLXPLAT_CPLD_AGGR_PSU_MASK_DEF | \
					 MLXPLAT_CPLD_AGGR_FAN_MASK_DEF)
#define MLXPLAT_CPLD_AGGR_MASK_MSN21XX	0x04
#define MLXPLAT_CPLD_PSU_MASK		GENMASK(1, 0)
#define MLXPLAT_CPLD_PWR_MASK		GENMASK(1, 0)
#define MLXPLAT_CPLD_FAN_MASK		GENMASK(3, 0)

/* Start channel numbers */
#define MLXPLAT_CPLD_CH1			2
#define MLXPLAT_CPLD_CH2			10

/* Number of LPC attached MUX platform devices */
#define MLXPLAT_CPLD_LPC_MUX_DEVS		2

/* mlxplat_priv - platform private data
 * @pdev_i2c - i2c controller platform device
 * @pdev_mux - array of mux platform devices
 */
struct mlxplat_priv {
	struct platform_device *pdev_i2c;
	struct platform_device *pdev_mux[MLXPLAT_CPLD_LPC_MUX_DEVS];
	struct platform_device *pdev_hotplug;
};

/* Regions for LPC I2C controller and LPC base register space */
static const struct resource mlxplat_lpc_resources[] = {
	[0] = DEFINE_RES_NAMED(MLXPLAT_CPLD_LPC_I2C_BASE_ADRR,
			       MLXPLAT_CPLD_LPC_IO_RANGE,
			       "mlxplat_cpld_lpc_i2c_ctrl", IORESOURCE_IO),
	[1] = DEFINE_RES_NAMED(MLXPLAT_CPLD_LPC_REG_BASE_ADRR,
			       MLXPLAT_CPLD_LPC_IO_RANGE,
			       "mlxplat_cpld_lpc_regs",
			       IORESOURCE_IO),
};

/* Platform default channels */
static const int mlxplat_default_channels[][8] = {
	{
		MLXPLAT_CPLD_CH1, MLXPLAT_CPLD_CH1 + 1, MLXPLAT_CPLD_CH1 + 2,
		MLXPLAT_CPLD_CH1 + 3, MLXPLAT_CPLD_CH1 + 4, MLXPLAT_CPLD_CH1 +
		5, MLXPLAT_CPLD_CH1 + 6, MLXPLAT_CPLD_CH1 + 7
	},
	{
		MLXPLAT_CPLD_CH2, MLXPLAT_CPLD_CH2 + 1, MLXPLAT_CPLD_CH2 + 2,
		MLXPLAT_CPLD_CH2 + 3, MLXPLAT_CPLD_CH2 + 4, MLXPLAT_CPLD_CH2 +
		5, MLXPLAT_CPLD_CH2 + 6, MLXPLAT_CPLD_CH2 + 7
	},
};

/* Platform channels for MSN21xx system family */
static const int mlxplat_msn21xx_channels[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

/* Platform mux data */
static struct i2c_mux_reg_platform_data mlxplat_mux_data[] = {
	{
		.parent = 1,
		.base_nr = MLXPLAT_CPLD_CH1,
		.write_only = 1,
		.reg = (void __iomem *)MLXPLAT_CPLD_LPC_REG1,
		.reg_size = 1,
		.idle_in_use = 1,
	},
	{
		.parent = 1,
		.base_nr = MLXPLAT_CPLD_CH2,
		.write_only = 1,
		.reg = (void __iomem *)MLXPLAT_CPLD_LPC_REG2,
		.reg_size = 1,
		.idle_in_use = 1,
	},

};

/* Platform hotplug devices */
static struct mlxcpld_hotplug_device mlxplat_mlxcpld_psu[] = {
	{
		.brdinfo = { I2C_BOARD_INFO("24c02", 0x51) },
		.bus = 10,
	},
	{
		.brdinfo = { I2C_BOARD_INFO("24c02", 0x50) },
		.bus = 10,
	},
};

static struct mlxcpld_hotplug_device mlxplat_mlxcpld_pwr[] = {
	{
		.brdinfo = { I2C_BOARD_INFO("dps460", 0x59) },
		.bus = 10,
	},
	{
		.brdinfo = { I2C_BOARD_INFO("dps460", 0x58) },
		.bus = 10,
	},
};

static struct mlxcpld_hotplug_device mlxplat_mlxcpld_fan[] = {
	{
		.brdinfo = { I2C_BOARD_INFO("24c32", 0x50) },
		.bus = 11,
	},
	{
		.brdinfo = { I2C_BOARD_INFO("24c32", 0x50) },
		.bus = 12,
	},
	{
		.brdinfo = { I2C_BOARD_INFO("24c32", 0x50) },
		.bus = 13,
	},
	{
		.brdinfo = { I2C_BOARD_INFO("24c32", 0x50) },
		.bus = 14,
	},
};

/* Platform hotplug default data */
static
struct mlxcpld_hotplug_platform_data mlxplat_mlxcpld_default_data = {
	.top_aggr_offset = MLXPLAT_CPLD_LPC_REG_AGGR_ADRR,
	.top_aggr_mask = MLXPLAT_CPLD_AGGR_MASK_DEF,
	.top_aggr_psu_mask = MLXPLAT_CPLD_AGGR_PSU_MASK_DEF,
	.psu_reg_offset = MLXPLAT_CPLD_LPC_REG_PSU_ADRR,
	.psu_mask = MLXPLAT_CPLD_PSU_MASK,
	.psu_count = ARRAY_SIZE(mlxplat_mlxcpld_psu),
	.psu = mlxplat_mlxcpld_psu,
	.top_aggr_pwr_mask = MLXPLAT_CPLD_AGGR_PWR_MASK_DEF,
	.pwr_reg_offset = MLXPLAT_CPLD_LPC_REG_PWR_ADRR,
	.pwr_mask = MLXPLAT_CPLD_PWR_MASK,
	.pwr_count = ARRAY_SIZE(mlxplat_mlxcpld_pwr),
	.pwr = mlxplat_mlxcpld_pwr,
	.top_aggr_fan_mask = MLXPLAT_CPLD_AGGR_FAN_MASK_DEF,
	.fan_reg_offset = MLXPLAT_CPLD_LPC_REG_FAN_ADRR,
	.fan_mask = MLXPLAT_CPLD_FAN_MASK,
	.fan_count = ARRAY_SIZE(mlxplat_mlxcpld_fan),
	.fan = mlxplat_mlxcpld_fan,
};

/* Platform hotplug MSN21xx system family data */
static
struct mlxcpld_hotplug_platform_data mlxplat_mlxcpld_msn21xx_data = {
	.top_aggr_offset = MLXPLAT_CPLD_LPC_REG_AGGR_ADRR,
	.top_aggr_mask = MLXPLAT_CPLD_AGGR_MASK_MSN21XX,
	.top_aggr_pwr_mask = MLXPLAT_CPLD_AGGR_MASK_MSN21XX,
	.pwr_reg_offset = MLXPLAT_CPLD_LPC_REG_PWR_ADRR,
	.pwr_mask = MLXPLAT_CPLD_PWR_MASK,
	.pwr_count = ARRAY_SIZE(mlxplat_mlxcpld_pwr),
};

static struct resource mlxplat_mlxcpld_resources[] = {
	[0] = DEFINE_RES_IRQ_NAMED(17, "mlxcpld-hotplug"),
};

static struct platform_device *mlxplat_dev;
static struct mlxcpld_hotplug_platform_data *mlxplat_hotplug;

static int __init mlxplat_dmi_default_matched(const struct dmi_system_id *dmi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxplat_mux_data); i++) {
		mlxplat_mux_data[i].values = mlxplat_default_channels[i];
		mlxplat_mux_data[i].n_values =
				ARRAY_SIZE(mlxplat_default_channels[i]);
	}
	mlxplat_hotplug = &mlxplat_mlxcpld_default_data;

	return 1;
};

static int __init mlxplat_dmi_msn21xx_matched(const struct dmi_system_id *dmi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxplat_mux_data); i++) {
		mlxplat_mux_data[i].values = mlxplat_msn21xx_channels;
		mlxplat_mux_data[i].n_values =
				ARRAY_SIZE(mlxplat_msn21xx_channels);
	}
	mlxplat_hotplug = &mlxplat_mlxcpld_msn21xx_data;

	return 1;
};

static const struct dmi_system_id mlxplat_dmi_table[] __initconst = {
	{
		.callback = mlxplat_dmi_default_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSN24"),
		},
	},
	{
		.callback = mlxplat_dmi_default_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSN27"),
		},
	},
	{
		.callback = mlxplat_dmi_default_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSB"),
		},
	},
	{
		.callback = mlxplat_dmi_default_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSX"),
		},
	},
	{
		.callback = mlxplat_dmi_msn21xx_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSN21"),
		},
	},
	{ }
};

static int __init mlxplat_init(void)
{
	struct mlxplat_priv *priv;
	int i, err;

	if (!dmi_check_system(mlxplat_dmi_table))
		return -ENODEV;

	mlxplat_dev = platform_device_register_simple(MLX_PLAT_DEVICE_NAME, -1,
					mlxplat_lpc_resources,
					ARRAY_SIZE(mlxplat_lpc_resources));

	if (IS_ERR(mlxplat_dev))
		return PTR_ERR(mlxplat_dev);

	priv = devm_kzalloc(&mlxplat_dev->dev, sizeof(struct mlxplat_priv),
			    GFP_KERNEL);
	if (!priv) {
		err = -ENOMEM;
		goto fail_alloc;
	}
	platform_set_drvdata(mlxplat_dev, priv);

	priv->pdev_i2c = platform_device_register_simple("i2c_mlxcpld", -1,
							 NULL, 0);
	if (IS_ERR(priv->pdev_i2c)) {
		err = PTR_ERR(priv->pdev_i2c);
		goto fail_alloc;
	}

	for (i = 0; i < ARRAY_SIZE(mlxplat_mux_data); i++) {
		priv->pdev_mux[i] = platform_device_register_resndata(
						&mlxplat_dev->dev,
						"i2c-mux-reg", i, NULL,
						0, &mlxplat_mux_data[i],
						sizeof(mlxplat_mux_data[i]));
		if (IS_ERR(priv->pdev_mux[i])) {
			err = PTR_ERR(priv->pdev_mux[i]);
			goto fail_platform_mux_register;
		}
	}

	priv->pdev_hotplug = platform_device_register_resndata(
				&mlxplat_dev->dev, "mlxcpld-hotplug",
				PLATFORM_DEVID_NONE,
				mlxplat_mlxcpld_resources,
				ARRAY_SIZE(mlxplat_mlxcpld_resources),
				mlxplat_hotplug, sizeof(*mlxplat_hotplug));
	if (IS_ERR(priv->pdev_hotplug)) {
		err = PTR_ERR(priv->pdev_hotplug);
		goto fail_platform_mux_register;
	}

	return 0;

fail_platform_mux_register:
	while (--i >= 0)
		platform_device_unregister(priv->pdev_mux[i]);
	platform_device_unregister(priv->pdev_i2c);
fail_alloc:
	platform_device_unregister(mlxplat_dev);

	return err;
}
module_init(mlxplat_init);

static void __exit mlxplat_exit(void)
{
	struct mlxplat_priv *priv = platform_get_drvdata(mlxplat_dev);
	int i;

	platform_device_unregister(priv->pdev_hotplug);

	for (i = ARRAY_SIZE(mlxplat_mux_data) - 1; i >= 0 ; i--)
		platform_device_unregister(priv->pdev_mux[i]);

	platform_device_unregister(priv->pdev_i2c);
	platform_device_unregister(mlxplat_dev);
}
module_exit(mlxplat_exit);

MODULE_AUTHOR("Vadim Pasternak (vadimp@mellanox.com)");
MODULE_DESCRIPTION("Mellanox platform driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("dmi:*:*Mellanox*:MSN24*:");
MODULE_ALIAS("dmi:*:*Mellanox*:MSN27*:");
MODULE_ALIAS("dmi:*:*Mellanox*:MSB*:");
MODULE_ALIAS("dmi:*:*Mellanox*:MSX*:");
MODULE_ALIAS("dmi:*:*Mellanox*:MSN21*:");
