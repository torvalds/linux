// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 * Mellanox platform driver
 *
 * Copyright (C) 2016-2018 Mellanox Technologies
 * Copyright (C) 2016-2018 Vadim Pasternak <vadimp@mellanox.com>
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/i2c-mux-reg.h>
#include <linux/platform_data/mlxreg.h>
#include <linux/regmap.h>

#define MLX_PLAT_DEVICE_NAME		"mlxplat"

/* LPC bus IO offsets */
#define MLXPLAT_CPLD_LPC_I2C_BASE_ADRR		0x2000
#define MLXPLAT_CPLD_LPC_REG_BASE_ADRR		0x2500
#define MLXPLAT_CPLD_LPC_REG_CPLD1_VER_OFFSET	0x00
#define MLXPLAT_CPLD_LPC_REG_CPLD2_VER_OFFSET	0x01
#define MLXPLAT_CPLD_LPC_REG_CPLD3_VER_OFFSET	0x02
#define MLXPLAT_CPLD_LPC_REG_CPLD4_VER_OFFSET	0x03
#define MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET	0x1d
#define MLXPLAT_CPLD_LPC_REG_RST_CAUSE1_OFFSET	0x1e
#define MLXPLAT_CPLD_LPC_REG_RST_CAUSE2_OFFSET	0x1f
#define MLXPLAT_CPLD_LPC_REG_LED1_OFFSET	0x20
#define MLXPLAT_CPLD_LPC_REG_LED2_OFFSET	0x21
#define MLXPLAT_CPLD_LPC_REG_LED3_OFFSET	0x22
#define MLXPLAT_CPLD_LPC_REG_LED4_OFFSET	0x23
#define MLXPLAT_CPLD_LPC_REG_LED5_OFFSET	0x24
#define MLXPLAT_CPLD_LPC_REG_FAN_DIRECTION	0x2a
#define MLXPLAT_CPLD_LPC_REG_GP1_OFFSET		0x30
#define MLXPLAT_CPLD_LPC_REG_WP1_OFFSET		0x31
#define MLXPLAT_CPLD_LPC_REG_GP2_OFFSET		0x32
#define MLXPLAT_CPLD_LPC_REG_WP2_OFFSET		0x33
#define MLXPLAT_CPLD_LPC_REG_PWM_CONTROL_OFFSET	0x37
#define MLXPLAT_CPLD_LPC_REG_AGGR_OFFSET	0x3a
#define MLXPLAT_CPLD_LPC_REG_AGGR_MASK_OFFSET	0x3b
#define MLXPLAT_CPLD_LPC_REG_AGGRLO_OFFSET	0x40
#define MLXPLAT_CPLD_LPC_REG_AGGRLO_MASK_OFFSET	0x41
#define MLXPLAT_CPLD_LPC_REG_AGGRCO_OFFSET	0x42
#define MLXPLAT_CPLD_LPC_REG_AGGRCO_MASK_OFFSET	0x43
#define MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET 0x50
#define MLXPLAT_CPLD_LPC_REG_ASIC_EVENT_OFFSET	0x51
#define MLXPLAT_CPLD_LPC_REG_ASIC_MASK_OFFSET	0x52
#define MLXPLAT_CPLD_LPC_REG_PSU_OFFSET		0x58
#define MLXPLAT_CPLD_LPC_REG_PSU_EVENT_OFFSET	0x59
#define MLXPLAT_CPLD_LPC_REG_PSU_MASK_OFFSET	0x5a
#define MLXPLAT_CPLD_LPC_REG_PWR_OFFSET		0x64
#define MLXPLAT_CPLD_LPC_REG_PWR_EVENT_OFFSET	0x65
#define MLXPLAT_CPLD_LPC_REG_PWR_MASK_OFFSET	0x66
#define MLXPLAT_CPLD_LPC_REG_FAN_OFFSET		0x88
#define MLXPLAT_CPLD_LPC_REG_FAN_EVENT_OFFSET	0x89
#define MLXPLAT_CPLD_LPC_REG_FAN_MASK_OFFSET	0x8a
#define MLXPLAT_CPLD_LPC_REG_WD_CLEAR_OFFSET	0xc7
#define MLXPLAT_CPLD_LPC_REG_WD_CLEAR_WP_OFFSET	0xc8
#define MLXPLAT_CPLD_LPC_REG_WD1_TMR_OFFSET	0xc9
#define MLXPLAT_CPLD_LPC_REG_WD1_ACT_OFFSET	0xcb
#define MLXPLAT_CPLD_LPC_REG_WD2_TMR_OFFSET	0xcd
#define MLXPLAT_CPLD_LPC_REG_WD2_TLEFT_OFFSET	0xce
#define MLXPLAT_CPLD_LPC_REG_WD2_ACT_OFFSET	0xcf
#define MLXPLAT_CPLD_LPC_REG_WD3_TMR_OFFSET	0xd1
#define MLXPLAT_CPLD_LPC_REG_WD3_TLEFT_OFFSET	0xd2
#define MLXPLAT_CPLD_LPC_REG_WD3_ACT_OFFSET	0xd3
#define MLXPLAT_CPLD_LPC_REG_PWM1_OFFSET	0xe3
#define MLXPLAT_CPLD_LPC_REG_TACHO1_OFFSET	0xe4
#define MLXPLAT_CPLD_LPC_REG_TACHO2_OFFSET	0xe5
#define MLXPLAT_CPLD_LPC_REG_TACHO3_OFFSET	0xe6
#define MLXPLAT_CPLD_LPC_REG_TACHO4_OFFSET	0xe7
#define MLXPLAT_CPLD_LPC_REG_TACHO5_OFFSET	0xe8
#define MLXPLAT_CPLD_LPC_REG_TACHO6_OFFSET	0xe9
#define MLXPLAT_CPLD_LPC_REG_TACHO7_OFFSET	0xeb
#define MLXPLAT_CPLD_LPC_REG_TACHO8_OFFSET	0xec
#define MLXPLAT_CPLD_LPC_REG_TACHO9_OFFSET	0xed
#define MLXPLAT_CPLD_LPC_REG_TACHO10_OFFSET	0xee
#define MLXPLAT_CPLD_LPC_REG_TACHO11_OFFSET	0xef
#define MLXPLAT_CPLD_LPC_REG_TACHO12_OFFSET	0xf0
#define MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET	0xf5
#define MLXPLAT_CPLD_LPC_REG_FAN_CAP2_OFFSET	0xf6
#define MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET	0xf7
#define MLXPLAT_CPLD_LPC_REG_TACHO_SPEED_OFFSET	0xf8
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
#define MLXPLAT_CPLD_AGGR_ASIC_MASK_DEF	0x04
#define MLXPLAT_CPLD_AGGR_PSU_MASK_DEF	0x08
#define MLXPLAT_CPLD_AGGR_PWR_MASK_DEF	0x08
#define MLXPLAT_CPLD_AGGR_FAN_MASK_DEF	0x40
#define MLXPLAT_CPLD_AGGR_MASK_DEF	(MLXPLAT_CPLD_AGGR_ASIC_MASK_DEF | \
					 MLXPLAT_CPLD_AGGR_PSU_MASK_DEF | \
					 MLXPLAT_CPLD_AGGR_FAN_MASK_DEF)
#define MLXPLAT_CPLD_AGGR_ASIC_MASK_NG	0x01
#define MLXPLAT_CPLD_AGGR_MASK_NG_DEF	0x04
#define MLXPLAT_CPLD_AGGR_MASK_COMEX	BIT(0)
#define MLXPLAT_CPLD_LOW_AGGR_MASK_LOW	0xc1
#define MLXPLAT_CPLD_LOW_AGGR_MASK_I2C	BIT(6)
#define MLXPLAT_CPLD_PSU_MASK		GENMASK(1, 0)
#define MLXPLAT_CPLD_PWR_MASK		GENMASK(1, 0)
#define MLXPLAT_CPLD_FAN_MASK		GENMASK(3, 0)
#define MLXPLAT_CPLD_ASIC_MASK		GENMASK(1, 0)
#define MLXPLAT_CPLD_FAN_NG_MASK	GENMASK(5, 0)
#define MLXPLAT_CPLD_LED_LO_NIBBLE_MASK	GENMASK(7, 4)
#define MLXPLAT_CPLD_LED_HI_NIBBLE_MASK	GENMASK(3, 0)

/* Default I2C parent bus number */
#define MLXPLAT_CPLD_PHYS_ADAPTER_DEF_NR	1

/* Maximum number of possible physical buses equipped on system */
#define MLXPLAT_CPLD_MAX_PHYS_ADAPTER_NUM	16

/* Number of channels in group */
#define MLXPLAT_CPLD_GRP_CHNL_NUM		8

/* Start channel numbers */
#define MLXPLAT_CPLD_CH1			2
#define MLXPLAT_CPLD_CH2			10

/* Number of LPC attached MUX platform devices */
#define MLXPLAT_CPLD_LPC_MUX_DEVS		2

/* Hotplug devices adapter numbers */
#define MLXPLAT_CPLD_NR_NONE			-1
#define MLXPLAT_CPLD_PSU_DEFAULT_NR		10
#define MLXPLAT_CPLD_PSU_MSNXXXX_NR		4
#define MLXPLAT_CPLD_FAN1_DEFAULT_NR		11
#define MLXPLAT_CPLD_FAN2_DEFAULT_NR		12
#define MLXPLAT_CPLD_FAN3_DEFAULT_NR		13
#define MLXPLAT_CPLD_FAN4_DEFAULT_NR		14

/* Masks and default values for watchdogs */
#define MLXPLAT_CPLD_WD1_CLEAR_MASK	GENMASK(7, 1)
#define MLXPLAT_CPLD_WD2_CLEAR_MASK	(GENMASK(7, 0) & ~BIT(1))

#define MLXPLAT_CPLD_WD_TYPE1_TO_MASK	GENMASK(7, 4)
#define MLXPLAT_CPLD_WD_TYPE2_TO_MASK	0
#define MLXPLAT_CPLD_WD_RESET_ACT_MASK	GENMASK(7, 1)
#define MLXPLAT_CPLD_WD_FAN_ACT_MASK	(GENMASK(7, 0) & ~BIT(4))
#define MLXPLAT_CPLD_WD_COUNT_ACT_MASK	(GENMASK(7, 0) & ~BIT(7))
#define MLXPLAT_CPLD_WD_DFLT_TIMEOUT	30
#define MLXPLAT_CPLD_WD_MAX_DEVS	2

/* mlxplat_priv - platform private data
 * @pdev_i2c - i2c controller platform device
 * @pdev_mux - array of mux platform devices
 * @pdev_hotplug - hotplug platform devices
 * @pdev_led - led platform devices
 * @pdev_io_regs - register access platform devices
 * @pdev_fan - FAN platform devices
 * @pdev_wd - array of watchdog platform devices
 * @regmap: device register map
 */
struct mlxplat_priv {
	struct platform_device *pdev_i2c;
	struct platform_device *pdev_mux[MLXPLAT_CPLD_LPC_MUX_DEVS];
	struct platform_device *pdev_hotplug;
	struct platform_device *pdev_led;
	struct platform_device *pdev_io_regs;
	struct platform_device *pdev_fan;
	struct platform_device *pdev_wd[MLXPLAT_CPLD_WD_MAX_DEVS];
	void *regmap;
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

/* Platform next generation systems i2c data */
static struct mlxreg_core_hotplug_platform_data mlxplat_mlxcpld_i2c_ng_data = {
	.cell = MLXPLAT_CPLD_LPC_REG_AGGR_OFFSET,
	.mask = MLXPLAT_CPLD_AGGR_MASK_COMEX,
	.cell_low = MLXPLAT_CPLD_LPC_REG_AGGRCO_OFFSET,
	.mask_low = MLXPLAT_CPLD_LOW_AGGR_MASK_I2C,
};

/* Platform default channels */
static const int mlxplat_default_channels[][MLXPLAT_CPLD_GRP_CHNL_NUM] = {
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
static struct i2c_board_info mlxplat_mlxcpld_psu[] = {
	{
		I2C_BOARD_INFO("24c02", 0x51),
	},
	{
		I2C_BOARD_INFO("24c02", 0x50),
	},
};

static struct i2c_board_info mlxplat_mlxcpld_ng_psu[] = {
	{
		I2C_BOARD_INFO("24c32", 0x51),
	},
	{
		I2C_BOARD_INFO("24c32", 0x50),
	},
};

static struct i2c_board_info mlxplat_mlxcpld_pwr[] = {
	{
		I2C_BOARD_INFO("dps460", 0x59),
	},
	{
		I2C_BOARD_INFO("dps460", 0x58),
	},
};

static struct i2c_board_info mlxplat_mlxcpld_fan[] = {
	{
		I2C_BOARD_INFO("24c32", 0x50),
	},
	{
		I2C_BOARD_INFO("24c32", 0x50),
	},
	{
		I2C_BOARD_INFO("24c32", 0x50),
	},
	{
		I2C_BOARD_INFO("24c32", 0x50),
	},
};

/* Platform hotplug default data */
static struct mlxreg_core_data mlxplat_mlxcpld_default_psu_items_data[] = {
	{
		.label = "psu1",
		.reg = MLXPLAT_CPLD_LPC_REG_PSU_OFFSET,
		.mask = BIT(0),
		.hpdev.brdinfo = &mlxplat_mlxcpld_psu[0],
		.hpdev.nr = MLXPLAT_CPLD_PSU_DEFAULT_NR,
	},
	{
		.label = "psu2",
		.reg = MLXPLAT_CPLD_LPC_REG_PSU_OFFSET,
		.mask = BIT(1),
		.hpdev.brdinfo = &mlxplat_mlxcpld_psu[1],
		.hpdev.nr = MLXPLAT_CPLD_PSU_DEFAULT_NR,
	},
};

static struct mlxreg_core_data mlxplat_mlxcpld_default_pwr_items_data[] = {
	{
		.label = "pwr1",
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = BIT(0),
		.hpdev.brdinfo = &mlxplat_mlxcpld_pwr[0],
		.hpdev.nr = MLXPLAT_CPLD_PSU_DEFAULT_NR,
	},
	{
		.label = "pwr2",
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = BIT(1),
		.hpdev.brdinfo = &mlxplat_mlxcpld_pwr[1],
		.hpdev.nr = MLXPLAT_CPLD_PSU_DEFAULT_NR,
	},
};

static struct mlxreg_core_data mlxplat_mlxcpld_default_fan_items_data[] = {
	{
		.label = "fan1",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(0),
		.hpdev.brdinfo = &mlxplat_mlxcpld_fan[0],
		.hpdev.nr = MLXPLAT_CPLD_FAN1_DEFAULT_NR,
	},
	{
		.label = "fan2",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(1),
		.hpdev.brdinfo = &mlxplat_mlxcpld_fan[1],
		.hpdev.nr = MLXPLAT_CPLD_FAN2_DEFAULT_NR,
	},
	{
		.label = "fan3",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(2),
		.hpdev.brdinfo = &mlxplat_mlxcpld_fan[2],
		.hpdev.nr = MLXPLAT_CPLD_FAN3_DEFAULT_NR,
	},
	{
		.label = "fan4",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(3),
		.hpdev.brdinfo = &mlxplat_mlxcpld_fan[3],
		.hpdev.nr = MLXPLAT_CPLD_FAN4_DEFAULT_NR,
	},
};

static struct mlxreg_core_data mlxplat_mlxcpld_default_asic_items_data[] = {
	{
		.label = "asic1",
		.reg = MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET,
		.mask = MLXPLAT_CPLD_ASIC_MASK,
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
};

static struct mlxreg_core_item mlxplat_mlxcpld_default_items[] = {
	{
		.data = mlxplat_mlxcpld_default_psu_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_PSU_MASK_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_PSU_OFFSET,
		.mask = MLXPLAT_CPLD_PSU_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_psu),
		.inversed = 1,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_pwr_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_PWR_MASK_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = MLXPLAT_CPLD_PWR_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_pwr),
		.inversed = 0,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_fan_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_FAN_MASK_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = MLXPLAT_CPLD_FAN_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_fan),
		.inversed = 1,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_asic_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_ASIC_MASK_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET,
		.mask = MLXPLAT_CPLD_ASIC_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_default_asic_items_data),
		.inversed = 0,
		.health = true,
	},
};

static
struct mlxreg_core_hotplug_platform_data mlxplat_mlxcpld_default_data = {
	.items = mlxplat_mlxcpld_default_items,
	.counter = ARRAY_SIZE(mlxplat_mlxcpld_default_items),
	.cell = MLXPLAT_CPLD_LPC_REG_AGGR_OFFSET,
	.mask = MLXPLAT_CPLD_AGGR_MASK_DEF,
	.cell_low = MLXPLAT_CPLD_LPC_REG_AGGRLO_OFFSET,
	.mask_low = MLXPLAT_CPLD_LOW_AGGR_MASK_LOW,
};

static struct mlxreg_core_data mlxplat_mlxcpld_msn21xx_pwr_items_data[] = {
	{
		.label = "pwr1",
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = BIT(0),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "pwr2",
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = BIT(1),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
};

/* Platform hotplug MSN21xx system family data */
static struct mlxreg_core_item mlxplat_mlxcpld_msn21xx_items[] = {
	{
		.data = mlxplat_mlxcpld_msn21xx_pwr_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_PWR_MASK_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = MLXPLAT_CPLD_PWR_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_msn21xx_pwr_items_data),
		.inversed = 0,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_asic_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_ASIC_MASK_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET,
		.mask = MLXPLAT_CPLD_ASIC_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_default_asic_items_data),
		.inversed = 0,
		.health = true,
	},
};

static
struct mlxreg_core_hotplug_platform_data mlxplat_mlxcpld_msn21xx_data = {
	.items = mlxplat_mlxcpld_msn21xx_items,
	.counter = ARRAY_SIZE(mlxplat_mlxcpld_msn21xx_items),
	.cell = MLXPLAT_CPLD_LPC_REG_AGGR_OFFSET,
	.mask = MLXPLAT_CPLD_AGGR_MASK_DEF,
	.cell_low = MLXPLAT_CPLD_LPC_REG_AGGRLO_OFFSET,
	.mask_low = MLXPLAT_CPLD_LOW_AGGR_MASK_LOW,
};

/* Platform hotplug msn274x system family data */
static struct mlxreg_core_data mlxplat_mlxcpld_msn274x_psu_items_data[] = {
	{
		.label = "psu1",
		.reg = MLXPLAT_CPLD_LPC_REG_PSU_OFFSET,
		.mask = BIT(0),
		.hpdev.brdinfo = &mlxplat_mlxcpld_psu[0],
		.hpdev.nr = MLXPLAT_CPLD_PSU_MSNXXXX_NR,
	},
	{
		.label = "psu2",
		.reg = MLXPLAT_CPLD_LPC_REG_PSU_OFFSET,
		.mask = BIT(1),
		.hpdev.brdinfo = &mlxplat_mlxcpld_psu[1],
		.hpdev.nr = MLXPLAT_CPLD_PSU_MSNXXXX_NR,
	},
};

static struct mlxreg_core_data mlxplat_mlxcpld_default_ng_pwr_items_data[] = {
	{
		.label = "pwr1",
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = BIT(0),
		.hpdev.brdinfo = &mlxplat_mlxcpld_pwr[0],
		.hpdev.nr = MLXPLAT_CPLD_PSU_MSNXXXX_NR,
	},
	{
		.label = "pwr2",
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = BIT(1),
		.hpdev.brdinfo = &mlxplat_mlxcpld_pwr[1],
		.hpdev.nr = MLXPLAT_CPLD_PSU_MSNXXXX_NR,
	},
};

static struct mlxreg_core_data mlxplat_mlxcpld_msn274x_fan_items_data[] = {
	{
		.label = "fan1",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(0),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "fan2",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(1),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "fan3",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(2),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "fan4",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(3),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
};

static struct mlxreg_core_item mlxplat_mlxcpld_msn274x_items[] = {
	{
		.data = mlxplat_mlxcpld_msn274x_psu_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_PSU_OFFSET,
		.mask = MLXPLAT_CPLD_PSU_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_msn274x_psu_items_data),
		.inversed = 1,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_ng_pwr_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = MLXPLAT_CPLD_PWR_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_default_ng_pwr_items_data),
		.inversed = 0,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_msn274x_fan_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = MLXPLAT_CPLD_FAN_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_msn274x_fan_items_data),
		.inversed = 1,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_asic_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET,
		.mask = MLXPLAT_CPLD_ASIC_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_default_asic_items_data),
		.inversed = 0,
		.health = true,
	},
};

static
struct mlxreg_core_hotplug_platform_data mlxplat_mlxcpld_msn274x_data = {
	.items = mlxplat_mlxcpld_msn274x_items,
	.counter = ARRAY_SIZE(mlxplat_mlxcpld_msn274x_items),
	.cell = MLXPLAT_CPLD_LPC_REG_AGGR_OFFSET,
	.mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF,
	.cell_low = MLXPLAT_CPLD_LPC_REG_AGGRLO_OFFSET,
	.mask_low = MLXPLAT_CPLD_LOW_AGGR_MASK_LOW,
};

/* Platform hotplug MSN201x system family data */
static struct mlxreg_core_data mlxplat_mlxcpld_msn201x_pwr_items_data[] = {
	{
		.label = "pwr1",
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = BIT(0),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "pwr2",
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = BIT(1),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
};

static struct mlxreg_core_item mlxplat_mlxcpld_msn201x_items[] = {
	{
		.data = mlxplat_mlxcpld_msn201x_pwr_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_PWR_MASK_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = MLXPLAT_CPLD_PWR_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_msn201x_pwr_items_data),
		.inversed = 0,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_asic_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_ASIC_MASK_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET,
		.mask = MLXPLAT_CPLD_ASIC_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_default_asic_items_data),
		.inversed = 0,
		.health = true,
	},
};

static
struct mlxreg_core_hotplug_platform_data mlxplat_mlxcpld_msn201x_data = {
	.items = mlxplat_mlxcpld_msn201x_items,
	.counter = ARRAY_SIZE(mlxplat_mlxcpld_msn201x_items),
	.cell = MLXPLAT_CPLD_LPC_REG_AGGR_OFFSET,
	.mask = MLXPLAT_CPLD_AGGR_MASK_DEF,
	.cell_low = MLXPLAT_CPLD_LPC_REG_AGGRLO_OFFSET,
	.mask_low = MLXPLAT_CPLD_LOW_AGGR_MASK_LOW,
};

/* Platform hotplug next generation system family data */
static struct mlxreg_core_data mlxplat_mlxcpld_default_ng_psu_items_data[] = {
	{
		.label = "psu1",
		.reg = MLXPLAT_CPLD_LPC_REG_PSU_OFFSET,
		.mask = BIT(0),
		.hpdev.brdinfo = &mlxplat_mlxcpld_ng_psu[0],
		.hpdev.nr = MLXPLAT_CPLD_PSU_MSNXXXX_NR,
	},
	{
		.label = "psu2",
		.reg = MLXPLAT_CPLD_LPC_REG_PSU_OFFSET,
		.mask = BIT(1),
		.hpdev.brdinfo = &mlxplat_mlxcpld_ng_psu[1],
		.hpdev.nr = MLXPLAT_CPLD_PSU_MSNXXXX_NR,
	},
};

static struct mlxreg_core_data mlxplat_mlxcpld_default_ng_fan_items_data[] = {
	{
		.label = "fan1",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(0),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "fan2",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(1),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(1),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "fan3",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(2),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(2),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "fan4",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(3),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(3),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "fan5",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(4),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(4),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
	{
		.label = "fan6",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = BIT(5),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(5),
		.hpdev.nr = MLXPLAT_CPLD_NR_NONE,
	},
};

static struct mlxreg_core_item mlxplat_mlxcpld_default_ng_items[] = {
	{
		.data = mlxplat_mlxcpld_default_ng_psu_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_PSU_OFFSET,
		.mask = MLXPLAT_CPLD_PSU_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_default_ng_psu_items_data),
		.inversed = 1,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_ng_pwr_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_PWR_OFFSET,
		.mask = MLXPLAT_CPLD_PWR_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_default_ng_pwr_items_data),
		.inversed = 0,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_ng_fan_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_OFFSET,
		.mask = MLXPLAT_CPLD_FAN_NG_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_default_ng_fan_items_data),
		.inversed = 1,
		.health = false,
	},
	{
		.data = mlxplat_mlxcpld_default_asic_items_data,
		.aggr_mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF,
		.reg = MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET,
		.mask = MLXPLAT_CPLD_ASIC_MASK,
		.count = ARRAY_SIZE(mlxplat_mlxcpld_default_asic_items_data),
		.inversed = 0,
		.health = true,
	},
};

static
struct mlxreg_core_hotplug_platform_data mlxplat_mlxcpld_default_ng_data = {
	.items = mlxplat_mlxcpld_default_ng_items,
	.counter = ARRAY_SIZE(mlxplat_mlxcpld_default_ng_items),
	.cell = MLXPLAT_CPLD_LPC_REG_AGGR_OFFSET,
	.mask = MLXPLAT_CPLD_AGGR_MASK_NG_DEF | MLXPLAT_CPLD_AGGR_MASK_COMEX,
	.cell_low = MLXPLAT_CPLD_LPC_REG_AGGRLO_OFFSET,
	.mask_low = MLXPLAT_CPLD_LOW_AGGR_MASK_LOW,
};

/* Platform led default data */
static struct mlxreg_core_data mlxplat_mlxcpld_default_led_data[] = {
	{
		.label = "status:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "status:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK
	},
	{
		.label = "psu:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
	{
		.label = "psu:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
	{
		.label = "fan1:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "fan1:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "fan2:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
	{
		.label = "fan2:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
	{
		.label = "fan3:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED3_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "fan3:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED3_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "fan4:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED3_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
	{
		.label = "fan4:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED3_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
};

static struct mlxreg_core_platform_data mlxplat_default_led_data = {
		.data = mlxplat_mlxcpld_default_led_data,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_default_led_data),
};

/* Platform led MSN21xx system family data */
static struct mlxreg_core_data mlxplat_mlxcpld_msn21xx_led_data[] = {
	{
		.label = "status:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "status:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK
	},
	{
		.label = "fan:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "fan:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "psu1:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED4_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "psu1:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED4_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "psu2:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED4_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
	{
		.label = "psu2:red",
		.reg = MLXPLAT_CPLD_LPC_REG_LED4_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
	{
		.label = "uid:blue",
		.reg = MLXPLAT_CPLD_LPC_REG_LED5_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
};

static struct mlxreg_core_platform_data mlxplat_msn21xx_led_data = {
		.data = mlxplat_mlxcpld_msn21xx_led_data,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_msn21xx_led_data),
};

/* Platform led for default data for 200GbE systems */
static struct mlxreg_core_data mlxplat_mlxcpld_default_ng_led_data[] = {
	{
		.label = "status:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
	{
		.label = "status:orange",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK
	},
	{
		.label = "psu:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
	{
		.label = "psu:orange",
		.reg = MLXPLAT_CPLD_LPC_REG_LED1_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
	},
	{
		.label = "fan1:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(0),
	},
	{
		.label = "fan1:orange",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(0),
	},
	{
		.label = "fan2:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(1),
	},
	{
		.label = "fan2:orange",
		.reg = MLXPLAT_CPLD_LPC_REG_LED2_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(1),
	},
	{
		.label = "fan3:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED3_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(2),
	},
	{
		.label = "fan3:orange",
		.reg = MLXPLAT_CPLD_LPC_REG_LED3_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(2),
	},
	{
		.label = "fan4:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED3_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(3),
	},
	{
		.label = "fan4:orange",
		.reg = MLXPLAT_CPLD_LPC_REG_LED3_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(3),
	},
	{
		.label = "fan5:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED4_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(4),
	},
	{
		.label = "fan5:orange",
		.reg = MLXPLAT_CPLD_LPC_REG_LED4_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(4),
	},
	{
		.label = "fan6:green",
		.reg = MLXPLAT_CPLD_LPC_REG_LED4_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(5),
	},
	{
		.label = "fan6:orange",
		.reg = MLXPLAT_CPLD_LPC_REG_LED4_OFFSET,
		.mask = MLXPLAT_CPLD_LED_HI_NIBBLE_MASK,
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET,
		.bit = BIT(5),
	},
	{
		.label = "uid:blue",
		.reg = MLXPLAT_CPLD_LPC_REG_LED5_OFFSET,
		.mask = MLXPLAT_CPLD_LED_LO_NIBBLE_MASK,
	},
};

static struct mlxreg_core_platform_data mlxplat_default_ng_led_data = {
		.data = mlxplat_mlxcpld_default_ng_led_data,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_default_ng_led_data),
};

/* Platform register access default */
static struct mlxreg_core_data mlxplat_mlxcpld_default_regs_io_data[] = {
	{
		.label = "cpld1_version",
		.reg = MLXPLAT_CPLD_LPC_REG_CPLD1_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld2_version",
		.reg = MLXPLAT_CPLD_LPC_REG_CPLD2_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "reset_long_pb",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0444,
	},
	{
		.label = "reset_short_pb",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0444,
	},
	{
		.label = "reset_aux_pwr_or_ref",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0444,
	},
	{
		.label = "reset_main_pwr_fail",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0444,
	},
	{
		.label = "reset_sw_reset",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(4),
		.mode = 0444,
	},
	{
		.label = "reset_fw_reset",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(5),
		.mode = 0444,
	},
	{
		.label = "reset_hotswap_or_wd",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0444,
	},
	{
		.label = "reset_asic_thermal",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(7),
		.mode = 0444,
	},
	{
		.label = "psu1_on",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0200,
	},
	{
		.label = "psu2_on",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0200,
	},
	{
		.label = "pwr_cycle",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0200,
	},
	{
		.label = "pwr_down",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0200,
	},
	{
		.label = "select_iio",
		.reg = MLXPLAT_CPLD_LPC_REG_GP2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0644,
	},
	{
		.label = "asic_health",
		.reg = MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET,
		.mask = MLXPLAT_CPLD_ASIC_MASK,
		.bit = 1,
		.mode = 0444,
	},
};

static struct mlxreg_core_platform_data mlxplat_default_regs_io_data = {
		.data = mlxplat_mlxcpld_default_regs_io_data,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_default_regs_io_data),
};

/* Platform register access MSN21xx, MSN201x, MSN274x systems families data */
static struct mlxreg_core_data mlxplat_mlxcpld_msn21xx_regs_io_data[] = {
	{
		.label = "cpld1_version",
		.reg = MLXPLAT_CPLD_LPC_REG_CPLD1_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld2_version",
		.reg = MLXPLAT_CPLD_LPC_REG_CPLD2_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "reset_long_pb",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0444,
	},
	{
		.label = "reset_short_pb",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0444,
	},
	{
		.label = "reset_aux_pwr_or_ref",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0444,
	},
	{
		.label = "reset_sw_reset",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0444,
	},
	{
		.label = "reset_main_pwr_fail",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(4),
		.mode = 0444,
	},
	{
		.label = "reset_asic_thermal",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(5),
		.mode = 0444,
	},
	{
		.label = "reset_hotswap_or_halt",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0444,
	},
	{
		.label = "reset_sff_wd",
		.reg = MLXPLAT_CPLD_LPC_REG_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0444,
	},
	{
		.label = "psu1_on",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0200,
	},
	{
		.label = "psu2_on",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0200,
	},
	{
		.label = "pwr_cycle",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0200,
	},
	{
		.label = "pwr_down",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0200,
	},
	{
		.label = "asic_health",
		.reg = MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET,
		.mask = MLXPLAT_CPLD_ASIC_MASK,
		.bit = 1,
		.mode = 0444,
	},
};

static struct mlxreg_core_platform_data mlxplat_msn21xx_regs_io_data = {
		.data = mlxplat_mlxcpld_msn21xx_regs_io_data,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_msn21xx_regs_io_data),
};

/* Platform register access for next generation systems families data */
static struct mlxreg_core_data mlxplat_mlxcpld_default_ng_regs_io_data[] = {
	{
		.label = "cpld1_version",
		.reg = MLXPLAT_CPLD_LPC_REG_CPLD1_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld2_version",
		.reg = MLXPLAT_CPLD_LPC_REG_CPLD2_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld3_version",
		.reg = MLXPLAT_CPLD_LPC_REG_CPLD3_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld4_version",
		.reg = MLXPLAT_CPLD_LPC_REG_CPLD4_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "reset_long_pb",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0444,
	},
	{
		.label = "reset_short_pb",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0444,
	},
	{
		.label = "reset_aux_pwr_or_ref",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0444,
	},
	{
		.label = "reset_from_comex",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(4),
		.mode = 0444,
	},
	{
		.label = "reset_from_asic",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(5),
		.mode = 0444,
	},
	{
		.label = "reset_swb_wd",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0444,
	},
	{
		.label = "reset_asic_thermal",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(7),
		.mode = 0444,
	},
	{
		.label = "reset_comex_pwr_fail",
		.reg = MLXPLAT_CPLD_LPC_REG_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0444,
	},
	{
		.label = "reset_comex_wd",
		.reg = MLXPLAT_CPLD_LPC_REG_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0444,
	},
	{
		.label = "reset_voltmon_upgrade_fail",
		.reg = MLXPLAT_CPLD_LPC_REG_RST_CAUSE2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0444,
	},
	{
		.label = "reset_system",
		.reg = MLXPLAT_CPLD_LPC_REG_RST_CAUSE2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0444,
	},
	{
		.label = "reset_comex_thermal",
		.reg = MLXPLAT_CPLD_LPC_REG_RST_CAUSE2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0444,
	},
	{
		.label = "reset_reload_bios",
		.reg = MLXPLAT_CPLD_LPC_REG_RST_CAUSE2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(5),
		.mode = 0444,
	},
	{
		.label = "psu1_on",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0200,
	},
	{
		.label = "psu2_on",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0200,
	},
	{
		.label = "pwr_cycle",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0200,
	},
	{
		.label = "pwr_down",
		.reg = MLXPLAT_CPLD_LPC_REG_GP1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0200,
	},
	{
		.label = "jtag_enable",
		.reg = MLXPLAT_CPLD_LPC_REG_GP2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(4),
		.mode = 0644,
	},
	{
		.label = "asic_health",
		.reg = MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET,
		.mask = MLXPLAT_CPLD_ASIC_MASK,
		.bit = 1,
		.mode = 0444,
	},
	{
		.label = "fan_dir",
		.reg = MLXPLAT_CPLD_LPC_REG_FAN_DIRECTION,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
};

static struct mlxreg_core_platform_data mlxplat_default_ng_regs_io_data = {
		.data = mlxplat_mlxcpld_default_ng_regs_io_data,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_default_ng_regs_io_data),
};

/* Platform FAN default */
static struct mlxreg_core_data mlxplat_mlxcpld_default_fan_data[] = {
	{
		.label = "pwm1",
		.reg = MLXPLAT_CPLD_LPC_REG_PWM1_OFFSET,
	},
	{
		.label = "tacho1",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO1_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET,
		.bit = BIT(0),
	},
	{
		.label = "tacho2",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO2_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET,
		.bit = BIT(1),
	},
	{
		.label = "tacho3",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO3_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET,
		.bit = BIT(2),
	},
	{
		.label = "tacho4",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO4_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET,
		.bit = BIT(3),
	},
	{
		.label = "tacho5",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO5_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET,
		.bit = BIT(4),
	},
	{
		.label = "tacho6",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO6_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET,
		.bit = BIT(5),
	},
	{
		.label = "tacho7",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO7_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET,
		.bit = BIT(6),
	},
	{
		.label = "tacho8",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO8_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET,
		.bit = BIT(7),
	},
	{
		.label = "tacho9",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO9_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP2_OFFSET,
		.bit = BIT(0),
	},
	{
		.label = "tacho10",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO10_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP2_OFFSET,
		.bit = BIT(1),
	},
	{
		.label = "tacho11",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO11_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP2_OFFSET,
		.bit = BIT(2),
	},
	{
		.label = "tacho12",
		.reg = MLXPLAT_CPLD_LPC_REG_TACHO12_OFFSET,
		.mask = GENMASK(7, 0),
		.capability = MLXPLAT_CPLD_LPC_REG_FAN_CAP2_OFFSET,
		.bit = BIT(3),
	},
	{
		.label = "conf",
		.capability = MLXPLAT_CPLD_LPC_REG_TACHO_SPEED_OFFSET,
	},
};

static struct mlxreg_core_platform_data mlxplat_default_fan_data = {
		.data = mlxplat_mlxcpld_default_fan_data,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_default_fan_data),
};

/* Watchdog type1: hardware implementation version1
 * (MSN2700, MSN2410, MSN2740, MSN2100 and MSN2140 systems).
 */
static struct mlxreg_core_data mlxplat_mlxcpld_wd_main_regs_type1[] = {
	{
		.label = "action",
		.reg = MLXPLAT_CPLD_LPC_REG_WD1_ACT_OFFSET,
		.mask = MLXPLAT_CPLD_WD_RESET_ACT_MASK,
		.bit = 0,
	},
	{
		.label = "timeout",
		.reg = MLXPLAT_CPLD_LPC_REG_WD1_TMR_OFFSET,
		.mask = MLXPLAT_CPLD_WD_TYPE1_TO_MASK,
		.health_cntr = MLXPLAT_CPLD_WD_DFLT_TIMEOUT,
	},
	{
		.label = "ping",
		.reg = MLXPLAT_CPLD_LPC_REG_WD_CLEAR_OFFSET,
		.mask = MLXPLAT_CPLD_WD1_CLEAR_MASK,
		.bit = 0,
	},
	{
		.label = "reset",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.bit = 6,
	},
};

static struct mlxreg_core_data mlxplat_mlxcpld_wd_aux_regs_type1[] = {
	{
		.label = "action",
		.reg = MLXPLAT_CPLD_LPC_REG_WD2_ACT_OFFSET,
		.mask = MLXPLAT_CPLD_WD_FAN_ACT_MASK,
		.bit = 4,
	},
	{
		.label = "timeout",
		.reg = MLXPLAT_CPLD_LPC_REG_WD2_TMR_OFFSET,
		.mask = MLXPLAT_CPLD_WD_TYPE1_TO_MASK,
		.health_cntr = MLXPLAT_CPLD_WD_DFLT_TIMEOUT,
	},
	{
		.label = "ping",
		.reg = MLXPLAT_CPLD_LPC_REG_WD_CLEAR_OFFSET,
		.mask = MLXPLAT_CPLD_WD1_CLEAR_MASK,
		.bit = 1,
	},
};

static struct mlxreg_core_platform_data mlxplat_mlxcpld_wd_set_type1[] = {
	{
		.data = mlxplat_mlxcpld_wd_main_regs_type1,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_wd_main_regs_type1),
		.version = MLX_WDT_TYPE1,
		.identity = "mlx-wdt-main",
	},
	{
		.data = mlxplat_mlxcpld_wd_aux_regs_type1,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_wd_aux_regs_type1),
		.version = MLX_WDT_TYPE1,
		.identity = "mlx-wdt-aux",
	},
};

/* Watchdog type2: hardware implementation version 2
 * (all systems except (MSN2700, MSN2410, MSN2740, MSN2100 and MSN2140).
 */
static struct mlxreg_core_data mlxplat_mlxcpld_wd_main_regs_type2[] = {
	{
		.label = "action",
		.reg = MLXPLAT_CPLD_LPC_REG_WD2_ACT_OFFSET,
		.mask = MLXPLAT_CPLD_WD_RESET_ACT_MASK,
		.bit = 0,
	},
	{
		.label = "timeout",
		.reg = MLXPLAT_CPLD_LPC_REG_WD2_TMR_OFFSET,
		.mask = MLXPLAT_CPLD_WD_TYPE2_TO_MASK,
		.health_cntr = MLXPLAT_CPLD_WD_DFLT_TIMEOUT,
	},
	{
		.label = "timeleft",
		.reg = MLXPLAT_CPLD_LPC_REG_WD2_TLEFT_OFFSET,
		.mask = MLXPLAT_CPLD_WD_TYPE2_TO_MASK,
	},
	{
		.label = "ping",
		.reg = MLXPLAT_CPLD_LPC_REG_WD2_ACT_OFFSET,
		.mask = MLXPLAT_CPLD_WD_RESET_ACT_MASK,
		.bit = 0,
	},
	{
		.label = "reset",
		.reg = MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.bit = 6,
	},
};

static struct mlxreg_core_data mlxplat_mlxcpld_wd_aux_regs_type2[] = {
	{
		.label = "action",
		.reg = MLXPLAT_CPLD_LPC_REG_WD3_ACT_OFFSET,
		.mask = MLXPLAT_CPLD_WD_FAN_ACT_MASK,
		.bit = 4,
	},
	{
		.label = "timeout",
		.reg = MLXPLAT_CPLD_LPC_REG_WD3_TMR_OFFSET,
		.mask = MLXPLAT_CPLD_WD_TYPE2_TO_MASK,
		.health_cntr = MLXPLAT_CPLD_WD_DFLT_TIMEOUT,
	},
	{
		.label = "timeleft",
		.reg = MLXPLAT_CPLD_LPC_REG_WD3_TLEFT_OFFSET,
		.mask = MLXPLAT_CPLD_WD_TYPE2_TO_MASK,
	},
	{
		.label = "ping",
		.reg = MLXPLAT_CPLD_LPC_REG_WD3_ACT_OFFSET,
		.mask = MLXPLAT_CPLD_WD_FAN_ACT_MASK,
		.bit = 4,
	},
};

static struct mlxreg_core_platform_data mlxplat_mlxcpld_wd_set_type2[] = {
	{
		.data = mlxplat_mlxcpld_wd_main_regs_type2,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_wd_main_regs_type2),
		.version = MLX_WDT_TYPE2,
		.identity = "mlx-wdt-main",
	},
	{
		.data = mlxplat_mlxcpld_wd_aux_regs_type2,
		.counter = ARRAY_SIZE(mlxplat_mlxcpld_wd_aux_regs_type2),
		.version = MLX_WDT_TYPE2,
		.identity = "mlx-wdt-aux",
	},
};

static bool mlxplat_mlxcpld_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MLXPLAT_CPLD_LPC_REG_LED1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED3_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED4_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED5_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_GP1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WP1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_GP2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WP2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGR_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRLO_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRCO_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_ASIC_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_ASIC_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PSU_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PSU_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWR_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWR_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD_CLEAR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD_CLEAR_WP_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD1_TMR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD1_ACT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD2_TMR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD2_ACT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD3_TMR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD3_ACT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWM1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWM_CONTROL_OFFSET:
		return true;
	}
	return false;
}

static bool mlxplat_mlxcpld_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MLXPLAT_CPLD_LPC_REG_CPLD1_VER_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_CPLD2_VER_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_CPLD3_VER_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_CPLD4_VER_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_RST_CAUSE1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_RST_CAUSE2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED3_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED4_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED5_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_DIRECTION:
	case MLXPLAT_CPLD_LPC_REG_GP1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WP1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_GP2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WP2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGR_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRLO_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRLO_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRCO_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRCO_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_ASIC_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_ASIC_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PSU_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PSU_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PSU_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWR_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWR_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD_CLEAR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD_CLEAR_WP_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD1_TMR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD1_ACT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD2_TMR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD2_TLEFT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD2_ACT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD3_TMR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD3_TLEFT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD3_ACT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWM1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO3_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO4_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO5_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO6_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO7_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO8_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO9_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO10_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO11_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO12_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWM_CONTROL_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_CAP2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO_SPEED_OFFSET:
		return true;
	}
	return false;
}

static bool mlxplat_mlxcpld_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MLXPLAT_CPLD_LPC_REG_CPLD1_VER_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_CPLD2_VER_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_CPLD3_VER_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_CPLD4_VER_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_RESET_CAUSE_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_RST_CAUSE1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_RST_CAUSE2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED3_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED4_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_LED5_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_DIRECTION:
	case MLXPLAT_CPLD_LPC_REG_GP1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_GP2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGR_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRLO_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRLO_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRCO_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_AGGRCO_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_ASIC_HEALTH_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_ASIC_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_ASIC_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PSU_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PSU_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PSU_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWR_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWR_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_EVENT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_MASK_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD2_TMR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD2_TLEFT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD3_TMR_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_WD3_TLEFT_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWM1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO3_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO4_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO5_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO6_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO7_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO8_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO9_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO10_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO11_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO12_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_PWM_CONTROL_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_CAP1_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_CAP2_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_FAN_DRW_CAP_OFFSET:
	case MLXPLAT_CPLD_LPC_REG_TACHO_SPEED_OFFSET:
		return true;
	}
	return false;
}

static const struct reg_default mlxplat_mlxcpld_regmap_default[] = {
	{ MLXPLAT_CPLD_LPC_REG_WP1_OFFSET, 0x00 },
	{ MLXPLAT_CPLD_LPC_REG_WP2_OFFSET, 0x00 },
	{ MLXPLAT_CPLD_LPC_REG_PWM_CONTROL_OFFSET, 0x00 },
	{ MLXPLAT_CPLD_LPC_REG_WD_CLEAR_WP_OFFSET, 0x00 },
};

static const struct reg_default mlxplat_mlxcpld_regmap_ng[] = {
	{ MLXPLAT_CPLD_LPC_REG_PWM_CONTROL_OFFSET, 0x00 },
	{ MLXPLAT_CPLD_LPC_REG_WD_CLEAR_WP_OFFSET, 0x00 },
};

struct mlxplat_mlxcpld_regmap_context {
	void __iomem *base;
};

static struct mlxplat_mlxcpld_regmap_context mlxplat_mlxcpld_regmap_ctx;

static int
mlxplat_mlxcpld_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct mlxplat_mlxcpld_regmap_context *ctx = context;

	*val = ioread8(ctx->base + reg);
	return 0;
}

static int
mlxplat_mlxcpld_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct mlxplat_mlxcpld_regmap_context *ctx = context;

	iowrite8(val, ctx->base + reg);
	return 0;
}

static const struct regmap_config mlxplat_mlxcpld_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 255,
	.cache_type = REGCACHE_FLAT,
	.writeable_reg = mlxplat_mlxcpld_writeable_reg,
	.readable_reg = mlxplat_mlxcpld_readable_reg,
	.volatile_reg = mlxplat_mlxcpld_volatile_reg,
	.reg_defaults = mlxplat_mlxcpld_regmap_default,
	.num_reg_defaults = ARRAY_SIZE(mlxplat_mlxcpld_regmap_default),
	.reg_read = mlxplat_mlxcpld_reg_read,
	.reg_write = mlxplat_mlxcpld_reg_write,
};

static const struct regmap_config mlxplat_mlxcpld_regmap_config_ng = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 255,
	.cache_type = REGCACHE_FLAT,
	.writeable_reg = mlxplat_mlxcpld_writeable_reg,
	.readable_reg = mlxplat_mlxcpld_readable_reg,
	.volatile_reg = mlxplat_mlxcpld_volatile_reg,
	.reg_defaults = mlxplat_mlxcpld_regmap_ng,
	.num_reg_defaults = ARRAY_SIZE(mlxplat_mlxcpld_regmap_ng),
	.reg_read = mlxplat_mlxcpld_reg_read,
	.reg_write = mlxplat_mlxcpld_reg_write,
};

static struct resource mlxplat_mlxcpld_resources[] = {
	[0] = DEFINE_RES_IRQ_NAMED(17, "mlxreg-hotplug"),
};

static struct platform_device *mlxplat_dev;
static struct mlxreg_core_hotplug_platform_data *mlxplat_i2c;
static struct mlxreg_core_hotplug_platform_data *mlxplat_hotplug;
static struct mlxreg_core_platform_data *mlxplat_led;
static struct mlxreg_core_platform_data *mlxplat_regs_io;
static struct mlxreg_core_platform_data *mlxplat_fan;
static struct mlxreg_core_platform_data
	*mlxplat_wd_data[MLXPLAT_CPLD_WD_MAX_DEVS];
static const struct regmap_config *mlxplat_regmap_config;

static int __init mlxplat_dmi_default_matched(const struct dmi_system_id *dmi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxplat_mux_data); i++) {
		mlxplat_mux_data[i].values = mlxplat_default_channels[i];
		mlxplat_mux_data[i].n_values =
				ARRAY_SIZE(mlxplat_default_channels[i]);
	}
	mlxplat_hotplug = &mlxplat_mlxcpld_default_data;
	mlxplat_hotplug->deferred_nr =
		mlxplat_default_channels[i - 1][MLXPLAT_CPLD_GRP_CHNL_NUM - 1];
	mlxplat_led = &mlxplat_default_led_data;
	mlxplat_regs_io = &mlxplat_default_regs_io_data;
	mlxplat_wd_data[0] = &mlxplat_mlxcpld_wd_set_type1[0];

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
	mlxplat_hotplug->deferred_nr =
		mlxplat_msn21xx_channels[MLXPLAT_CPLD_GRP_CHNL_NUM - 1];
	mlxplat_led = &mlxplat_msn21xx_led_data;
	mlxplat_regs_io = &mlxplat_msn21xx_regs_io_data;
	mlxplat_wd_data[0] = &mlxplat_mlxcpld_wd_set_type1[0];

	return 1;
};

static int __init mlxplat_dmi_msn274x_matched(const struct dmi_system_id *dmi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxplat_mux_data); i++) {
		mlxplat_mux_data[i].values = mlxplat_msn21xx_channels;
		mlxplat_mux_data[i].n_values =
				ARRAY_SIZE(mlxplat_msn21xx_channels);
	}
	mlxplat_hotplug = &mlxplat_mlxcpld_msn274x_data;
	mlxplat_hotplug->deferred_nr =
		mlxplat_msn21xx_channels[MLXPLAT_CPLD_GRP_CHNL_NUM - 1];
	mlxplat_led = &mlxplat_default_led_data;
	mlxplat_regs_io = &mlxplat_msn21xx_regs_io_data;
	mlxplat_wd_data[0] = &mlxplat_mlxcpld_wd_set_type1[0];

	return 1;
};

static int __init mlxplat_dmi_msn201x_matched(const struct dmi_system_id *dmi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxplat_mux_data); i++) {
		mlxplat_mux_data[i].values = mlxplat_msn21xx_channels;
		mlxplat_mux_data[i].n_values =
				ARRAY_SIZE(mlxplat_msn21xx_channels);
	}
	mlxplat_hotplug = &mlxplat_mlxcpld_msn201x_data;
	mlxplat_hotplug->deferred_nr =
		mlxplat_default_channels[i - 1][MLXPLAT_CPLD_GRP_CHNL_NUM - 1];
	mlxplat_led = &mlxplat_msn21xx_led_data;
	mlxplat_regs_io = &mlxplat_msn21xx_regs_io_data;
	mlxplat_wd_data[0] = &mlxplat_mlxcpld_wd_set_type1[0];

	return 1;
};

static int __init mlxplat_dmi_qmb7xx_matched(const struct dmi_system_id *dmi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxplat_mux_data); i++) {
		mlxplat_mux_data[i].values = mlxplat_msn21xx_channels;
		mlxplat_mux_data[i].n_values =
				ARRAY_SIZE(mlxplat_msn21xx_channels);
	}
	mlxplat_hotplug = &mlxplat_mlxcpld_default_ng_data;
	mlxplat_hotplug->deferred_nr =
		mlxplat_msn21xx_channels[MLXPLAT_CPLD_GRP_CHNL_NUM - 1];
	mlxplat_led = &mlxplat_default_ng_led_data;
	mlxplat_regs_io = &mlxplat_default_ng_regs_io_data;
	mlxplat_fan = &mlxplat_default_fan_data;
	for (i = 0; i < ARRAY_SIZE(mlxplat_mlxcpld_wd_set_type2); i++)
		mlxplat_wd_data[i] = &mlxplat_mlxcpld_wd_set_type2[i];
	mlxplat_i2c = &mlxplat_mlxcpld_i2c_ng_data;
	mlxplat_regmap_config = &mlxplat_mlxcpld_regmap_config_ng;

	return 1;
};

static const struct dmi_system_id mlxplat_dmi_table[] __initconst = {
	{
		.callback = mlxplat_dmi_default_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "VMOD0001"),
		},
	},
	{
		.callback = mlxplat_dmi_msn21xx_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "VMOD0002"),
		},
	},
	{
		.callback = mlxplat_dmi_msn274x_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "VMOD0003"),
		},
	},
	{
		.callback = mlxplat_dmi_msn201x_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "VMOD0004"),
		},
	},
	{
		.callback = mlxplat_dmi_qmb7xx_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "VMOD0005"),
		},
	},
	{
		.callback = mlxplat_dmi_qmb7xx_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "VMOD0007"),
		},
	},
	{
		.callback = mlxplat_dmi_msn274x_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSN274"),
		},
	},
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
	{
		.callback = mlxplat_dmi_msn201x_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSN201"),
		},
	},
	{
		.callback = mlxplat_dmi_qmb7xx_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MQM87"),
		},
	},
	{
		.callback = mlxplat_dmi_qmb7xx_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSN37"),
		},
	},
	{
		.callback = mlxplat_dmi_qmb7xx_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSN34"),
		},
	},
	{
		.callback = mlxplat_dmi_qmb7xx_matched,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Mellanox Technologies"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MSN38"),
		},
	},
	{ }
};

MODULE_DEVICE_TABLE(dmi, mlxplat_dmi_table);

static int mlxplat_mlxcpld_verify_bus_topology(int *nr)
{
	struct i2c_adapter *search_adap;
	int shift, i;

	/* Scan adapters from expected id to verify it is free. */
	*nr = MLXPLAT_CPLD_PHYS_ADAPTER_DEF_NR;
	for (i = MLXPLAT_CPLD_PHYS_ADAPTER_DEF_NR; i <
	     MLXPLAT_CPLD_MAX_PHYS_ADAPTER_NUM; i++) {
		search_adap = i2c_get_adapter(i);
		if (search_adap) {
			i2c_put_adapter(search_adap);
			continue;
		}

		/* Return if expected parent adapter is free. */
		if (i == MLXPLAT_CPLD_PHYS_ADAPTER_DEF_NR)
			return 0;
		break;
	}

	/* Return with error if free id for adapter is not found. */
	if (i == MLXPLAT_CPLD_MAX_PHYS_ADAPTER_NUM)
		return -ENODEV;

	/* Shift adapter ids, since expected parent adapter is not free. */
	*nr = i;
	for (i = 0; i < ARRAY_SIZE(mlxplat_mux_data); i++) {
		shift = *nr - mlxplat_mux_data[i].parent;
		mlxplat_mux_data[i].parent = *nr;
		mlxplat_mux_data[i].base_nr += shift;
		if (shift > 0)
			mlxplat_hotplug->shift_nr = shift;
	}

	return 0;
}

static int __init mlxplat_init(void)
{
	struct mlxplat_priv *priv;
	int i, j, nr, err;

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

	mlxplat_mlxcpld_regmap_ctx.base = devm_ioport_map(&mlxplat_dev->dev,
			       mlxplat_lpc_resources[1].start, 1);
	if (!mlxplat_mlxcpld_regmap_ctx.base) {
		err = -ENOMEM;
		goto fail_alloc;
	}

	if (!mlxplat_regmap_config)
		mlxplat_regmap_config = &mlxplat_mlxcpld_regmap_config;

	priv->regmap = devm_regmap_init(&mlxplat_dev->dev, NULL,
					&mlxplat_mlxcpld_regmap_ctx,
					mlxplat_regmap_config);
	if (IS_ERR(priv->regmap)) {
		err = PTR_ERR(priv->regmap);
		return err;
	}

	err = mlxplat_mlxcpld_verify_bus_topology(&nr);
	if (nr < 0)
		goto fail_alloc;

	nr = (nr == MLXPLAT_CPLD_MAX_PHYS_ADAPTER_NUM) ? -1 : nr;
	if (mlxplat_i2c)
		mlxplat_i2c->regmap = priv->regmap;
	priv->pdev_i2c = platform_device_register_resndata(
					&mlxplat_dev->dev, "i2c_mlxcpld",
					nr, mlxplat_mlxcpld_resources,
					ARRAY_SIZE(mlxplat_mlxcpld_resources),
					mlxplat_i2c, sizeof(*mlxplat_i2c));
	if (IS_ERR(priv->pdev_i2c)) {
		err = PTR_ERR(priv->pdev_i2c);
		goto fail_alloc;
	}

	for (i = 0; i < ARRAY_SIZE(mlxplat_mux_data); i++) {
		priv->pdev_mux[i] = platform_device_register_resndata(
						&priv->pdev_i2c->dev,
						"i2c-mux-reg", i, NULL,
						0, &mlxplat_mux_data[i],
						sizeof(mlxplat_mux_data[i]));
		if (IS_ERR(priv->pdev_mux[i])) {
			err = PTR_ERR(priv->pdev_mux[i]);
			goto fail_platform_mux_register;
		}
	}

	/* Add hotplug driver */
	mlxplat_hotplug->regmap = priv->regmap;
	priv->pdev_hotplug = platform_device_register_resndata(
				&mlxplat_dev->dev, "mlxreg-hotplug",
				PLATFORM_DEVID_NONE,
				mlxplat_mlxcpld_resources,
				ARRAY_SIZE(mlxplat_mlxcpld_resources),
				mlxplat_hotplug, sizeof(*mlxplat_hotplug));
	if (IS_ERR(priv->pdev_hotplug)) {
		err = PTR_ERR(priv->pdev_hotplug);
		goto fail_platform_mux_register;
	}

	/* Set default registers. */
	for (j = 0; j <  mlxplat_regmap_config->num_reg_defaults; j++) {
		err = regmap_write(priv->regmap,
				   mlxplat_regmap_config->reg_defaults[j].reg,
				   mlxplat_regmap_config->reg_defaults[j].def);
		if (err)
			goto fail_platform_mux_register;
	}

	/* Add LED driver. */
	mlxplat_led->regmap = priv->regmap;
	priv->pdev_led = platform_device_register_resndata(
				&mlxplat_dev->dev, "leds-mlxreg",
				PLATFORM_DEVID_NONE, NULL, 0,
				mlxplat_led, sizeof(*mlxplat_led));
	if (IS_ERR(priv->pdev_led)) {
		err = PTR_ERR(priv->pdev_led);
		goto fail_platform_hotplug_register;
	}

	/* Add registers io access driver. */
	if (mlxplat_regs_io) {
		mlxplat_regs_io->regmap = priv->regmap;
		priv->pdev_io_regs = platform_device_register_resndata(
					&mlxplat_dev->dev, "mlxreg-io",
					PLATFORM_DEVID_NONE, NULL, 0,
					mlxplat_regs_io,
					sizeof(*mlxplat_regs_io));
		if (IS_ERR(priv->pdev_io_regs)) {
			err = PTR_ERR(priv->pdev_io_regs);
			goto fail_platform_led_register;
		}
	}

	/* Add FAN driver. */
	if (mlxplat_fan) {
		mlxplat_fan->regmap = priv->regmap;
		priv->pdev_fan = platform_device_register_resndata(
					&mlxplat_dev->dev, "mlxreg-fan",
					PLATFORM_DEVID_NONE, NULL, 0,
					mlxplat_fan,
					sizeof(*mlxplat_fan));
		if (IS_ERR(priv->pdev_fan)) {
			err = PTR_ERR(priv->pdev_fan);
			goto fail_platform_io_regs_register;
		}
	}

	/* Add WD drivers. */
	for (j = 0; j < MLXPLAT_CPLD_WD_MAX_DEVS; j++) {
		if (mlxplat_wd_data[j]) {
			mlxplat_wd_data[j]->regmap = priv->regmap;
			priv->pdev_wd[j] = platform_device_register_resndata(
						&mlxplat_dev->dev, "mlx-wdt",
						j, NULL, 0,
						mlxplat_wd_data[j],
						sizeof(*mlxplat_wd_data[j]));
			if (IS_ERR(priv->pdev_wd[j])) {
				err = PTR_ERR(priv->pdev_wd[j]);
				goto fail_platform_wd_register;
			}
		}
	}

	/* Sync registers with hardware. */
	regcache_mark_dirty(priv->regmap);
	err = regcache_sync(priv->regmap);
	if (err)
		goto fail_platform_wd_register;

	return 0;

fail_platform_wd_register:
	while (--j >= 0)
		platform_device_unregister(priv->pdev_wd[j]);
	if (mlxplat_fan)
		platform_device_unregister(priv->pdev_fan);
fail_platform_io_regs_register:
	if (mlxplat_regs_io)
		platform_device_unregister(priv->pdev_io_regs);
fail_platform_led_register:
	platform_device_unregister(priv->pdev_led);
fail_platform_hotplug_register:
	platform_device_unregister(priv->pdev_hotplug);
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

	for (i = MLXPLAT_CPLD_WD_MAX_DEVS - 1; i >= 0 ; i--)
		platform_device_unregister(priv->pdev_wd[i]);
	if (priv->pdev_fan)
		platform_device_unregister(priv->pdev_fan);
	if (priv->pdev_io_regs)
		platform_device_unregister(priv->pdev_io_regs);
	platform_device_unregister(priv->pdev_led);
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
