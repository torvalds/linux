// SPDX-License-Identifier: GPL-2.0+
/*
 * Nvidia sn2201 driver
 *
 * Copyright (C) 2022 Nvidia Technologies Ltd.
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/platform_data/mlxcpld.h>
#include <linux/platform_data/mlxreg.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* SN2201 CPLD register offset. */
#define NVSW_SN2201_CPLD_LPC_I2C_BASE_ADRR          0x2000
#define NVSW_SN2201_CPLD_LPC_IO_RANGE               0x100
#define NVSW_SN2201_HW_VER_ID_OFFSET                0x00
#define NVSW_SN2201_BOARD_ID_OFFSET                 0x01
#define NVSW_SN2201_CPLD_VER_OFFSET                 0x02
#define NVSW_SN2201_CPLD_MVER_OFFSET                0x03
#define NVSW_SN2201_CPLD_ID_OFFSET                  0x04
#define NVSW_SN2201_CPLD_PN_OFFSET                  0x05
#define NVSW_SN2201_CPLD_PN1_OFFSET                 0x06
#define NVSW_SN2201_PSU_CTRL_OFFSET                 0x0a
#define NVSW_SN2201_QSFP28_STATUS_OFFSET            0x0b
#define NVSW_SN2201_QSFP28_INT_STATUS_OFFSET        0x0c
#define NVSW_SN2201_QSFP28_LP_STATUS_OFFSET         0x0d
#define NVSW_SN2201_QSFP28_RST_STATUS_OFFSET        0x0e
#define NVSW_SN2201_SYS_STATUS_OFFSET               0x0f
#define NVSW_SN2201_FRONT_SYS_LED_CTRL_OFFSET       0x10
#define NVSW_SN2201_FRONT_PSU_LED_CTRL_OFFSET       0x12
#define NVSW_SN2201_FRONT_UID_LED_CTRL_OFFSET       0x13
#define NVSW_SN2201_QSFP28_LED_TEST_STATUS_OFFSET   0x14
#define NVSW_SN2201_SYS_RST_STATUS_OFFSET           0x15
#define NVSW_SN2201_SYS_INT_STATUS_OFFSET           0x21
#define NVSW_SN2201_SYS_INT_MASK_OFFSET             0x22
#define NVSW_SN2201_ASIC_STATUS_OFFSET              0x24
#define NVSW_SN2201_ASIC_EVENT_OFFSET               0x25
#define NVSW_SN2201_ASIC_MAKS_OFFSET                0x26
#define NVSW_SN2201_THML_STATUS_OFFSET              0x27
#define NVSW_SN2201_THML_EVENT_OFFSET               0x28
#define NVSW_SN2201_THML_MASK_OFFSET                0x29
#define NVSW_SN2201_PS_ALT_STATUS_OFFSET            0x2a
#define NVSW_SN2201_PS_ALT_EVENT_OFFSET             0x2b
#define NVSW_SN2201_PS_ALT_MASK_OFFSET              0x2c
#define NVSW_SN2201_PS_PRSNT_STATUS_OFFSET          0x30
#define NVSW_SN2201_PS_PRSNT_EVENT_OFFSET           0x31
#define NVSW_SN2201_PS_PRSNT_MASK_OFFSET            0x32
#define NVSW_SN2201_PS_DC_OK_STATUS_OFFSET          0x33
#define NVSW_SN2201_PS_DC_OK_EVENT_OFFSET           0x34
#define NVSW_SN2201_PS_DC_OK_MASK_OFFSET            0x35
#define NVSW_SN2201_RST_CAUSE1_OFFSET               0x36
#define NVSW_SN2201_RST_CAUSE2_OFFSET               0x37
#define NVSW_SN2201_RST_SW_CTRL_OFFSET              0x38
#define NVSW_SN2201_FAN_PRSNT_STATUS_OFFSET         0x3a
#define NVSW_SN2201_FAN_PRSNT_EVENT_OFFSET          0x3b
#define NVSW_SN2201_FAN_PRSNT_MASK_OFFSET           0x3c
#define NVSW_SN2201_WD_TMR_OFFSET_LSB               0x40
#define NVSW_SN2201_WD_TMR_OFFSET_MSB               0x41
#define NVSW_SN2201_WD_ACT_OFFSET                   0x42
#define NVSW_SN2201_FAN_LED1_CTRL_OFFSET            0x50
#define NVSW_SN2201_FAN_LED2_CTRL_OFFSET            0x51
#define NVSW_SN2201_REG_MAX                         0x52

/* Number of physical I2C busses. */
#define NVSW_SN2201_PHY_I2C_BUS_NUM		2
/* Number of main mux channels. */
#define NVSW_SN2201_MAIN_MUX_CHNL_NUM		8

#define NVSW_SN2201_MAIN_NR			0
#define NVSW_SN2201_MAIN_MUX_NR			1
#define NVSW_SN2201_MAIN_MUX_DEFER_NR		(NVSW_SN2201_PHY_I2C_BUS_NUM + \
						 NVSW_SN2201_MAIN_MUX_CHNL_NUM - 1)

#define NVSW_SN2201_MAIN_MUX_CH0_NR	NVSW_SN2201_PHY_I2C_BUS_NUM
#define NVSW_SN2201_MAIN_MUX_CH1_NR	(NVSW_SN2201_MAIN_MUX_CH0_NR + 1)
#define NVSW_SN2201_MAIN_MUX_CH2_NR	(NVSW_SN2201_MAIN_MUX_CH0_NR + 2)
#define NVSW_SN2201_MAIN_MUX_CH3_NR	(NVSW_SN2201_MAIN_MUX_CH0_NR + 3)
#define NVSW_SN2201_MAIN_MUX_CH5_NR	(NVSW_SN2201_MAIN_MUX_CH0_NR + 5)
#define NVSW_SN2201_MAIN_MUX_CH6_NR	(NVSW_SN2201_MAIN_MUX_CH0_NR + 6)
#define NVSW_SN2201_MAIN_MUX_CH7_NR	(NVSW_SN2201_MAIN_MUX_CH0_NR + 7)
#define NVSW_SN2201_2ND_MUX_CH0_NR	(NVSW_SN2201_MAIN_MUX_CH7_NR + 1)
#define NVSW_SN2201_2ND_MUX_CH1_NR	(NVSW_SN2201_MAIN_MUX_CH7_NR + 2)
#define NVSW_SN2201_2ND_MUX_CH2_NR	(NVSW_SN2201_MAIN_MUX_CH7_NR + 3)
#define NVSW_SN2201_2ND_MUX_CH3_NR	(NVSW_SN2201_MAIN_MUX_CH7_NR + 4)

#define NVSW_SN2201_CPLD_NR		NVSW_SN2201_MAIN_MUX_CH0_NR
#define NVSW_SN2201_NR_NONE		-1

/* Masks for aggregation, PSU presence and power, ASIC events
 * in CPLD related registers.
 */
#define NVSW_SN2201_CPLD_AGGR_ASIC_MASK_DEF	0xe0
#define NVSW_SN2201_CPLD_AGGR_PSU_MASK_DEF	0x04
#define NVSW_SN2201_CPLD_AGGR_PWR_MASK_DEF	0x02
#define NVSW_SN2201_CPLD_AGGR_FAN_MASK_DEF	0x10
#define NVSW_SN2201_CPLD_AGGR_MASK_DEF      \
	(NVSW_SN2201_CPLD_AGGR_ASIC_MASK_DEF \
	| NVSW_SN2201_CPLD_AGGR_PSU_MASK_DEF \
	| NVSW_SN2201_CPLD_AGGR_PWR_MASK_DEF \
	| NVSW_SN2201_CPLD_AGGR_FAN_MASK_DEF)
#define NVSW_SN2201_CPLD_AGGR_BUSBAR_MASK_DEF \
	(NVSW_SN2201_CPLD_AGGR_ASIC_MASK_DEF \
	| NVSW_SN2201_CPLD_AGGR_FAN_MASK_DEF)

#define NVSW_SN2201_CPLD_ASIC_MASK		GENMASK(3, 1)
#define NVSW_SN2201_CPLD_PSU_MASK		GENMASK(1, 0)
#define NVSW_SN2201_CPLD_PWR_MASK		GENMASK(1, 0)
#define NVSW_SN2201_CPLD_FAN_MASK		GENMASK(3, 0)

#define NVSW_SN2201_CPLD_SYSIRQ			26
#define NVSW_SN2201_LPC_SYSIRQ			28
#define NVSW_SN2201_CPLD_I2CADDR		0x41

#define NVSW_SN2201_WD_DFLT_TIMEOUT		600

/* nvsw_sn2201 - device private data
 * @dev: platform device;
 * @io_data: register access platform data;
 * @led_data: LED platform data;
 * @hotplug_data: hotplug platform data;
 * @i2c_data: I2C controller platform data;
 * @led: LED device;
 * @io_regs: register access device;
 * @pdev_hotplug: hotplug device;
 * @sn2201_devs: I2C devices for sn2201 devices;
 * @sn2201_devs_num: number of I2C devices for sn2201 device;
 * @main_mux_devs: I2C devices for main mux;
 * @main_mux_devs_num: number of I2C devices for main mux;
 * @cpld_devs: I2C devices for cpld;
 * @cpld_devs_num: number of I2C devices for cpld;
 * @main_mux_deferred_nr: I2C adapter number must be exist prior creating devices execution;
 * @ext_pwr_source: true if system powered by external power supply; false - by internal;
 */
struct nvsw_sn2201 {
	struct device *dev;
	struct mlxreg_core_platform_data *io_data;
	struct mlxreg_core_platform_data *led_data;
	struct mlxreg_core_platform_data *wd_data;
	struct mlxreg_core_hotplug_platform_data *hotplug_data;
	struct mlxreg_core_hotplug_platform_data *i2c_data;
	struct platform_device *led;
	struct platform_device *wd;
	struct platform_device *io_regs;
	struct platform_device *pdev_hotplug;
	struct platform_device *pdev_i2c;
	struct mlxreg_hotplug_device *sn2201_devs;
	int sn2201_devs_num;
	struct mlxreg_hotplug_device *main_mux_devs;
	int main_mux_devs_num;
	struct mlxreg_hotplug_device *cpld_devs;
	int cpld_devs_num;
	int main_mux_deferred_nr;
	bool ext_pwr_source;
};

static bool nvsw_sn2201_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NVSW_SN2201_PSU_CTRL_OFFSET:
	case NVSW_SN2201_QSFP28_LP_STATUS_OFFSET:
	case NVSW_SN2201_QSFP28_RST_STATUS_OFFSET:
	case NVSW_SN2201_FRONT_SYS_LED_CTRL_OFFSET:
	case NVSW_SN2201_FRONT_PSU_LED_CTRL_OFFSET:
	case NVSW_SN2201_FRONT_UID_LED_CTRL_OFFSET:
	case NVSW_SN2201_QSFP28_LED_TEST_STATUS_OFFSET:
	case NVSW_SN2201_SYS_RST_STATUS_OFFSET:
	case NVSW_SN2201_SYS_INT_MASK_OFFSET:
	case NVSW_SN2201_ASIC_EVENT_OFFSET:
	case NVSW_SN2201_ASIC_MAKS_OFFSET:
	case NVSW_SN2201_THML_EVENT_OFFSET:
	case NVSW_SN2201_THML_MASK_OFFSET:
	case NVSW_SN2201_PS_ALT_EVENT_OFFSET:
	case NVSW_SN2201_PS_ALT_MASK_OFFSET:
	case NVSW_SN2201_PS_PRSNT_EVENT_OFFSET:
	case NVSW_SN2201_PS_PRSNT_MASK_OFFSET:
	case NVSW_SN2201_PS_DC_OK_EVENT_OFFSET:
	case NVSW_SN2201_PS_DC_OK_MASK_OFFSET:
	case NVSW_SN2201_RST_SW_CTRL_OFFSET:
	case NVSW_SN2201_FAN_PRSNT_EVENT_OFFSET:
	case NVSW_SN2201_FAN_PRSNT_MASK_OFFSET:
	case NVSW_SN2201_WD_TMR_OFFSET_LSB:
	case NVSW_SN2201_WD_TMR_OFFSET_MSB:
	case NVSW_SN2201_WD_ACT_OFFSET:
	case NVSW_SN2201_FAN_LED1_CTRL_OFFSET:
	case NVSW_SN2201_FAN_LED2_CTRL_OFFSET:
		return true;
	}
	return false;
}

static bool nvsw_sn2201_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NVSW_SN2201_HW_VER_ID_OFFSET:
	case NVSW_SN2201_BOARD_ID_OFFSET:
	case NVSW_SN2201_CPLD_VER_OFFSET:
	case NVSW_SN2201_CPLD_MVER_OFFSET:
	case NVSW_SN2201_CPLD_ID_OFFSET:
	case NVSW_SN2201_CPLD_PN_OFFSET:
	case NVSW_SN2201_CPLD_PN1_OFFSET:
	case NVSW_SN2201_PSU_CTRL_OFFSET:
	case NVSW_SN2201_QSFP28_STATUS_OFFSET:
	case NVSW_SN2201_QSFP28_INT_STATUS_OFFSET:
	case NVSW_SN2201_QSFP28_LP_STATUS_OFFSET:
	case NVSW_SN2201_QSFP28_RST_STATUS_OFFSET:
	case NVSW_SN2201_SYS_STATUS_OFFSET:
	case NVSW_SN2201_FRONT_SYS_LED_CTRL_OFFSET:
	case NVSW_SN2201_FRONT_PSU_LED_CTRL_OFFSET:
	case NVSW_SN2201_FRONT_UID_LED_CTRL_OFFSET:
	case NVSW_SN2201_QSFP28_LED_TEST_STATUS_OFFSET:
	case NVSW_SN2201_SYS_RST_STATUS_OFFSET:
	case NVSW_SN2201_RST_CAUSE1_OFFSET:
	case NVSW_SN2201_RST_CAUSE2_OFFSET:
	case NVSW_SN2201_SYS_INT_STATUS_OFFSET:
	case NVSW_SN2201_SYS_INT_MASK_OFFSET:
	case NVSW_SN2201_ASIC_STATUS_OFFSET:
	case NVSW_SN2201_ASIC_EVENT_OFFSET:
	case NVSW_SN2201_ASIC_MAKS_OFFSET:
	case NVSW_SN2201_THML_STATUS_OFFSET:
	case NVSW_SN2201_THML_EVENT_OFFSET:
	case NVSW_SN2201_THML_MASK_OFFSET:
	case NVSW_SN2201_PS_ALT_STATUS_OFFSET:
	case NVSW_SN2201_PS_ALT_EVENT_OFFSET:
	case NVSW_SN2201_PS_ALT_MASK_OFFSET:
	case NVSW_SN2201_PS_PRSNT_STATUS_OFFSET:
	case NVSW_SN2201_PS_PRSNT_EVENT_OFFSET:
	case NVSW_SN2201_PS_PRSNT_MASK_OFFSET:
	case NVSW_SN2201_PS_DC_OK_STATUS_OFFSET:
	case NVSW_SN2201_PS_DC_OK_EVENT_OFFSET:
	case NVSW_SN2201_PS_DC_OK_MASK_OFFSET:
	case NVSW_SN2201_RST_SW_CTRL_OFFSET:
	case NVSW_SN2201_FAN_PRSNT_STATUS_OFFSET:
	case NVSW_SN2201_FAN_PRSNT_EVENT_OFFSET:
	case NVSW_SN2201_FAN_PRSNT_MASK_OFFSET:
	case NVSW_SN2201_WD_TMR_OFFSET_LSB:
	case NVSW_SN2201_WD_TMR_OFFSET_MSB:
	case NVSW_SN2201_WD_ACT_OFFSET:
	case NVSW_SN2201_FAN_LED1_CTRL_OFFSET:
	case NVSW_SN2201_FAN_LED2_CTRL_OFFSET:
		return true;
	}
	return false;
}

static bool nvsw_sn2201_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NVSW_SN2201_HW_VER_ID_OFFSET:
	case NVSW_SN2201_BOARD_ID_OFFSET:
	case NVSW_SN2201_CPLD_VER_OFFSET:
	case NVSW_SN2201_CPLD_MVER_OFFSET:
	case NVSW_SN2201_CPLD_ID_OFFSET:
	case NVSW_SN2201_CPLD_PN_OFFSET:
	case NVSW_SN2201_CPLD_PN1_OFFSET:
	case NVSW_SN2201_PSU_CTRL_OFFSET:
	case NVSW_SN2201_QSFP28_STATUS_OFFSET:
	case NVSW_SN2201_QSFP28_INT_STATUS_OFFSET:
	case NVSW_SN2201_QSFP28_LP_STATUS_OFFSET:
	case NVSW_SN2201_QSFP28_RST_STATUS_OFFSET:
	case NVSW_SN2201_SYS_STATUS_OFFSET:
	case NVSW_SN2201_FRONT_SYS_LED_CTRL_OFFSET:
	case NVSW_SN2201_FRONT_PSU_LED_CTRL_OFFSET:
	case NVSW_SN2201_FRONT_UID_LED_CTRL_OFFSET:
	case NVSW_SN2201_QSFP28_LED_TEST_STATUS_OFFSET:
	case NVSW_SN2201_SYS_RST_STATUS_OFFSET:
	case NVSW_SN2201_RST_CAUSE1_OFFSET:
	case NVSW_SN2201_RST_CAUSE2_OFFSET:
	case NVSW_SN2201_SYS_INT_STATUS_OFFSET:
	case NVSW_SN2201_SYS_INT_MASK_OFFSET:
	case NVSW_SN2201_ASIC_STATUS_OFFSET:
	case NVSW_SN2201_ASIC_EVENT_OFFSET:
	case NVSW_SN2201_ASIC_MAKS_OFFSET:
	case NVSW_SN2201_THML_STATUS_OFFSET:
	case NVSW_SN2201_THML_EVENT_OFFSET:
	case NVSW_SN2201_THML_MASK_OFFSET:
	case NVSW_SN2201_PS_ALT_STATUS_OFFSET:
	case NVSW_SN2201_PS_ALT_EVENT_OFFSET:
	case NVSW_SN2201_PS_ALT_MASK_OFFSET:
	case NVSW_SN2201_PS_PRSNT_STATUS_OFFSET:
	case NVSW_SN2201_PS_PRSNT_EVENT_OFFSET:
	case NVSW_SN2201_PS_PRSNT_MASK_OFFSET:
	case NVSW_SN2201_PS_DC_OK_STATUS_OFFSET:
	case NVSW_SN2201_PS_DC_OK_EVENT_OFFSET:
	case NVSW_SN2201_PS_DC_OK_MASK_OFFSET:
	case NVSW_SN2201_RST_SW_CTRL_OFFSET:
	case NVSW_SN2201_FAN_PRSNT_STATUS_OFFSET:
	case NVSW_SN2201_FAN_PRSNT_EVENT_OFFSET:
	case NVSW_SN2201_FAN_PRSNT_MASK_OFFSET:
	case NVSW_SN2201_WD_TMR_OFFSET_LSB:
	case NVSW_SN2201_WD_TMR_OFFSET_MSB:
	case NVSW_SN2201_FAN_LED1_CTRL_OFFSET:
	case NVSW_SN2201_FAN_LED2_CTRL_OFFSET:
		return true;
	}
	return false;
}

static const struct reg_default nvsw_sn2201_regmap_default[] = {
	{ NVSW_SN2201_QSFP28_LED_TEST_STATUS_OFFSET, 0x00 },
	{ NVSW_SN2201_WD_ACT_OFFSET, 0x00 },
};

/* Configuration for the register map of a device with 1 bytes address space. */
static const struct regmap_config nvsw_sn2201_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = NVSW_SN2201_REG_MAX,
	.cache_type = REGCACHE_FLAT,
	.writeable_reg = nvsw_sn2201_writeable_reg,
	.readable_reg = nvsw_sn2201_readable_reg,
	.volatile_reg = nvsw_sn2201_volatile_reg,
	.reg_defaults = nvsw_sn2201_regmap_default,
	.num_reg_defaults = ARRAY_SIZE(nvsw_sn2201_regmap_default),
};

/* Regions for LPC I2C controller and LPC base register space. */
static const struct resource nvsw_sn2201_lpc_io_resources[] = {
	[0] = DEFINE_RES_NAMED(NVSW_SN2201_CPLD_LPC_I2C_BASE_ADRR,
			       NVSW_SN2201_CPLD_LPC_IO_RANGE,
			       "mlxplat_cpld_lpc_i2c_ctrl", IORESOURCE_IO),
};

static struct resource nvsw_sn2201_cpld_res[] = {
	[0] = DEFINE_RES_IRQ_NAMED(NVSW_SN2201_CPLD_SYSIRQ, "mlxreg-hotplug"),
};

static struct resource nvsw_sn2201_lpc_res[] = {
	[0] = DEFINE_RES_IRQ_NAMED(NVSW_SN2201_LPC_SYSIRQ, "i2c-mlxcpld"),
};

/* SN2201 I2C platform data. */
static struct mlxreg_core_hotplug_platform_data nvsw_sn2201_i2c_data = {
	.irq = NVSW_SN2201_CPLD_SYSIRQ,
};

/* SN2201 CPLD device. */
static struct i2c_board_info nvsw_sn2201_cpld_devices[] = {
	{
		I2C_BOARD_INFO("nvsw-sn2201", 0x41),
	},
};

/* SN2201 CPLD board info. */
static struct mlxreg_hotplug_device nvsw_sn2201_cpld_brdinfo[] = {
	{
		.brdinfo = &nvsw_sn2201_cpld_devices[0],
		.nr = NVSW_SN2201_CPLD_NR,
	},
};

/* SN2201 main mux device. */
static struct i2c_board_info nvsw_sn2201_main_mux_devices[] = {
	{
		I2C_BOARD_INFO("pca9548", 0x70),
	},
};

/* SN2201 main mux board info. */
static struct mlxreg_hotplug_device nvsw_sn2201_main_mux_brdinfo[] = {
	{
		.brdinfo = &nvsw_sn2201_main_mux_devices[0],
		.nr = NVSW_SN2201_MAIN_MUX_NR,
	},
};

/* SN2201 power devices. */
static struct i2c_board_info nvsw_sn2201_pwr_devices[] = {
	{
		I2C_BOARD_INFO("pmbus", 0x58),
	},
	{
		I2C_BOARD_INFO("pmbus", 0x58),
	},
};

/* SN2201 fan devices. */
static struct i2c_board_info nvsw_sn2201_fan_devices[] = {
	{
		I2C_BOARD_INFO("24c02", 0x50),
	},
	{
		I2C_BOARD_INFO("24c02", 0x51),
	},
	{
		I2C_BOARD_INFO("24c02", 0x52),
	},
	{
		I2C_BOARD_INFO("24c02", 0x53),
	},
};

/* SN2201 hotplug default data. */
static struct mlxreg_core_data nvsw_sn2201_psu_items_data[] = {
	{
		.label = "psu1",
		.reg = NVSW_SN2201_PS_PRSNT_STATUS_OFFSET,
		.mask = BIT(0),
		.hpdev.nr = NVSW_SN2201_NR_NONE,
	},
	{
		.label = "psu2",
		.reg = NVSW_SN2201_PS_PRSNT_STATUS_OFFSET,
		.mask = BIT(1),
		.hpdev.nr = NVSW_SN2201_NR_NONE,
	},
};

static struct mlxreg_core_data nvsw_sn2201_pwr_items_data[] = {
	{
		.label = "pwr1",
		.reg = NVSW_SN2201_PS_DC_OK_STATUS_OFFSET,
		.mask = BIT(0),
		.hpdev.brdinfo = &nvsw_sn2201_pwr_devices[0],
		.hpdev.nr = NVSW_SN2201_MAIN_MUX_CH1_NR,
	},
	{
		.label = "pwr2",
		.reg = NVSW_SN2201_PS_DC_OK_STATUS_OFFSET,
		.mask = BIT(1),
		.hpdev.brdinfo = &nvsw_sn2201_pwr_devices[1],
		.hpdev.nr = NVSW_SN2201_MAIN_MUX_CH2_NR,
	},
};

static struct mlxreg_core_data nvsw_sn2201_fan_items_data[] = {
	{
		.label = "fan1",
		.reg = NVSW_SN2201_FAN_PRSNT_STATUS_OFFSET,
		.mask = BIT(0),
		.hpdev.brdinfo = &nvsw_sn2201_fan_devices[0],
		.hpdev.nr = NVSW_SN2201_2ND_MUX_CH0_NR,
	},
	{
		.label = "fan2",
		.reg = NVSW_SN2201_FAN_PRSNT_STATUS_OFFSET,
		.mask = BIT(1),
		.hpdev.brdinfo = &nvsw_sn2201_fan_devices[1],
		.hpdev.nr = NVSW_SN2201_2ND_MUX_CH1_NR,
	},
	{
		.label = "fan3",
		.reg = NVSW_SN2201_FAN_PRSNT_STATUS_OFFSET,
		.mask = BIT(2),
		.hpdev.brdinfo = &nvsw_sn2201_fan_devices[2],
		.hpdev.nr = NVSW_SN2201_2ND_MUX_CH2_NR,
	},
	{
		.label = "fan4",
		.reg = NVSW_SN2201_FAN_PRSNT_STATUS_OFFSET,
		.mask = BIT(3),
		.hpdev.brdinfo = &nvsw_sn2201_fan_devices[3],
		.hpdev.nr = NVSW_SN2201_2ND_MUX_CH3_NR,
	},
};

static struct mlxreg_core_data nvsw_sn2201_sys_items_data[] = {
	{
		.label = "nic_smb_alert",
		.reg = NVSW_SN2201_ASIC_STATUS_OFFSET,
		.mask = BIT(1),
		.hpdev.nr = NVSW_SN2201_NR_NONE,
	},
	{
		.label = "cpu_sd",
		.reg = NVSW_SN2201_ASIC_STATUS_OFFSET,
		.mask = BIT(2),
		.hpdev.nr = NVSW_SN2201_NR_NONE,
	},
	{
		.label = "mac_health",
		.reg = NVSW_SN2201_ASIC_STATUS_OFFSET,
		.mask = BIT(3),
		.hpdev.nr = NVSW_SN2201_NR_NONE,
	},
};

static struct mlxreg_core_item nvsw_sn2201_items[] = {
	{
		.data = nvsw_sn2201_psu_items_data,
		.aggr_mask = NVSW_SN2201_CPLD_AGGR_PSU_MASK_DEF,
		.reg = NVSW_SN2201_PS_PRSNT_STATUS_OFFSET,
		.mask = NVSW_SN2201_CPLD_PSU_MASK,
		.count = ARRAY_SIZE(nvsw_sn2201_psu_items_data),
		.inversed = 1,
		.health = false,
	},
	{
		.data = nvsw_sn2201_pwr_items_data,
		.aggr_mask = NVSW_SN2201_CPLD_AGGR_PWR_MASK_DEF,
		.reg = NVSW_SN2201_PS_DC_OK_STATUS_OFFSET,
		.mask = NVSW_SN2201_CPLD_PWR_MASK,
		.count = ARRAY_SIZE(nvsw_sn2201_pwr_items_data),
		.inversed = 0,
		.health = false,
	},
	{
		.data = nvsw_sn2201_fan_items_data,
		.aggr_mask = NVSW_SN2201_CPLD_AGGR_FAN_MASK_DEF,
		.reg = NVSW_SN2201_FAN_PRSNT_STATUS_OFFSET,
		.mask = NVSW_SN2201_CPLD_FAN_MASK,
		.count = ARRAY_SIZE(nvsw_sn2201_fan_items_data),
		.inversed = 1,
		.health = false,
	},
	{
		.data = nvsw_sn2201_sys_items_data,
		.aggr_mask = NVSW_SN2201_CPLD_AGGR_ASIC_MASK_DEF,
		.reg = NVSW_SN2201_ASIC_STATUS_OFFSET,
		.mask = NVSW_SN2201_CPLD_ASIC_MASK,
		.count = ARRAY_SIZE(nvsw_sn2201_sys_items_data),
		.inversed = 1,
		.health = false,
	},
};

static
struct mlxreg_core_hotplug_platform_data nvsw_sn2201_hotplug = {
	.items = nvsw_sn2201_items,
	.count = ARRAY_SIZE(nvsw_sn2201_items),
	.cell = NVSW_SN2201_SYS_INT_STATUS_OFFSET,
	.mask = NVSW_SN2201_CPLD_AGGR_MASK_DEF,
};

static struct mlxreg_core_item nvsw_sn2201_busbar_items[] = {
	{
		.data = nvsw_sn2201_fan_items_data,
		.aggr_mask = NVSW_SN2201_CPLD_AGGR_FAN_MASK_DEF,
		.reg = NVSW_SN2201_FAN_PRSNT_STATUS_OFFSET,
		.mask = NVSW_SN2201_CPLD_FAN_MASK,
		.count = ARRAY_SIZE(nvsw_sn2201_fan_items_data),
		.inversed = 1,
		.health = false,
	},
	{
		.data = nvsw_sn2201_sys_items_data,
		.aggr_mask = NVSW_SN2201_CPLD_AGGR_ASIC_MASK_DEF,
		.reg = NVSW_SN2201_ASIC_STATUS_OFFSET,
		.mask = NVSW_SN2201_CPLD_ASIC_MASK,
		.count = ARRAY_SIZE(nvsw_sn2201_sys_items_data),
		.inversed = 1,
		.health = false,
	},
};

static
struct mlxreg_core_hotplug_platform_data nvsw_sn2201_busbar_hotplug = {
	.items = nvsw_sn2201_busbar_items,
	.count = ARRAY_SIZE(nvsw_sn2201_busbar_items),
	.cell = NVSW_SN2201_SYS_INT_STATUS_OFFSET,
	.mask = NVSW_SN2201_CPLD_AGGR_BUSBAR_MASK_DEF,
};

/* SN2201 static devices. */
static struct i2c_board_info nvsw_sn2201_static_devices[] = {
	{
		I2C_BOARD_INFO("24c02", 0x57),
	},
	{
		I2C_BOARD_INFO("lm75", 0x4b),
	},
	{
		I2C_BOARD_INFO("24c64", 0x56),
	},
	{
		I2C_BOARD_INFO("ads1015", 0x49),
	},
	{
		I2C_BOARD_INFO("pca9546", 0x71),
	},
	{
		I2C_BOARD_INFO("emc2305", 0x4d),
	},
	{
		I2C_BOARD_INFO("lm75", 0x49),
	},
	{
		I2C_BOARD_INFO("pca9555", 0x27),
	},
	{
		I2C_BOARD_INFO("powr1014", 0x37),
	},
	{
		I2C_BOARD_INFO("lm75", 0x4f),
	},
	{
		I2C_BOARD_INFO("pmbus", 0x40),
	},
	{
		I2C_BOARD_INFO("lm5066i", 0x15),
	},
};

/* SN2201 default static board info. */
static struct mlxreg_hotplug_device nvsw_sn2201_static_brdinfo[] = {
	{
		.brdinfo = &nvsw_sn2201_static_devices[0],
		.nr = NVSW_SN2201_MAIN_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[1],
		.nr = NVSW_SN2201_MAIN_MUX_CH0_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[2],
		.nr = NVSW_SN2201_MAIN_MUX_CH0_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[3],
		.nr = NVSW_SN2201_MAIN_MUX_CH0_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[4],
		.nr = NVSW_SN2201_MAIN_MUX_CH3_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[5],
		.nr = NVSW_SN2201_MAIN_MUX_CH5_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[6],
		.nr = NVSW_SN2201_MAIN_MUX_CH5_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[7],
		.nr = NVSW_SN2201_MAIN_MUX_CH5_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[8],
		.nr = NVSW_SN2201_MAIN_MUX_CH6_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[9],
		.nr = NVSW_SN2201_MAIN_MUX_CH6_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[10],
		.nr = NVSW_SN2201_MAIN_MUX_CH7_NR,
	},
};

/* SN2201 default busbar static board info. */
static struct mlxreg_hotplug_device nvsw_sn2201_busbar_static_brdinfo[] = {
	{
		.brdinfo = &nvsw_sn2201_static_devices[0],
		.nr = NVSW_SN2201_MAIN_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[1],
		.nr = NVSW_SN2201_MAIN_MUX_CH0_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[2],
		.nr = NVSW_SN2201_MAIN_MUX_CH0_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[3],
		.nr = NVSW_SN2201_MAIN_MUX_CH0_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[4],
		.nr = NVSW_SN2201_MAIN_MUX_CH3_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[5],
		.nr = NVSW_SN2201_MAIN_MUX_CH5_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[6],
		.nr = NVSW_SN2201_MAIN_MUX_CH5_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[7],
		.nr = NVSW_SN2201_MAIN_MUX_CH5_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[8],
		.nr = NVSW_SN2201_MAIN_MUX_CH6_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[9],
		.nr = NVSW_SN2201_MAIN_MUX_CH6_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[10],
		.nr = NVSW_SN2201_MAIN_MUX_CH7_NR,
	},
	{
		.brdinfo = &nvsw_sn2201_static_devices[11],
		.nr = NVSW_SN2201_MAIN_MUX_CH1_NR,
	},
};

/* LED default data. */
static struct mlxreg_core_data nvsw_sn2201_led_data[] = {
	{
		.label = "status:green",
		.reg = NVSW_SN2201_FRONT_SYS_LED_CTRL_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "status:orange",
		.reg = NVSW_SN2201_FRONT_SYS_LED_CTRL_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "psu:green",
		.reg = NVSW_SN2201_FRONT_PSU_LED_CTRL_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "psu:orange",
		.reg = NVSW_SN2201_FRONT_PSU_LED_CTRL_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "uid:blue",
		.reg = NVSW_SN2201_FRONT_UID_LED_CTRL_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "fan1:green",
		.reg = NVSW_SN2201_FAN_LED1_CTRL_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "fan1:orange",
		.reg = NVSW_SN2201_FAN_LED1_CTRL_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "fan2:green",
		.reg = NVSW_SN2201_FAN_LED1_CTRL_OFFSET,
		.mask = GENMASK(3, 0),
	},
	{
		.label = "fan2:orange",
		.reg = NVSW_SN2201_FAN_LED1_CTRL_OFFSET,
		.mask = GENMASK(3, 0),
	},
	{
		.label = "fan3:green",
		.reg = NVSW_SN2201_FAN_LED2_CTRL_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "fan3:orange",
		.reg = NVSW_SN2201_FAN_LED2_CTRL_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "fan4:green",
		.reg = NVSW_SN2201_FAN_LED2_CTRL_OFFSET,
		.mask = GENMASK(3, 0),
	},
	{
		.label = "fan4:orange",
		.reg = NVSW_SN2201_FAN_LED2_CTRL_OFFSET,
		.mask = GENMASK(3, 0),
	},
};

static struct mlxreg_core_platform_data nvsw_sn2201_led = {
	.data = nvsw_sn2201_led_data,
	.counter = ARRAY_SIZE(nvsw_sn2201_led_data),
};

/* Default register access data. */
static struct mlxreg_core_data nvsw_sn2201_io_data[] = {
	{
		.label = "cpld1_version",
		.reg = NVSW_SN2201_CPLD_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld1_version_min",
		.reg = NVSW_SN2201_CPLD_MVER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld1_pn",
		.reg = NVSW_SN2201_CPLD_PN_OFFSET,
		.bit = GENMASK(15, 0),
		.mode = 0444,
		.regnum = 2,
	},
	{
		.label = "psu1_on",
		.reg = NVSW_SN2201_PSU_CTRL_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0644,
	},
	{
		.label = "psu2_on",
		.reg = NVSW_SN2201_PSU_CTRL_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0644,
	},
	{
		.label = "pwr_cycle",
		.reg = NVSW_SN2201_PSU_CTRL_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0644,
	},
	{
		.label = "asic_health",
		.reg = NVSW_SN2201_SYS_STATUS_OFFSET,
		.mask = GENMASK(4, 3),
		.bit = 4,
		.mode = 0444,
	},
	{
		.label = "qsfp_pwr_good",
		.reg = NVSW_SN2201_SYS_STATUS_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0444,
	},
	{
		.label = "phy_reset",
		.reg = NVSW_SN2201_SYS_RST_STATUS_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0644,
	},
	{
		.label = "mac_reset",
		.reg = NVSW_SN2201_SYS_RST_STATUS_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0644,
	},
	{
		.label = "pwr_down",
		.reg = NVSW_SN2201_RST_SW_CTRL_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0644,
	},
	{
		.label = "reset_long_pb",
		.reg = NVSW_SN2201_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0444,
	},
	{
		.label = "reset_short_pb",
		.reg = NVSW_SN2201_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0444,
	},
	{
		.label = "reset_aux_pwr_or_fu",
		.reg = NVSW_SN2201_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0444,
	},
	{
		.label = "reset_swb_dc_dc_pwr_fail",
		.reg = NVSW_SN2201_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0444,
	},
	{
		.label = "reset_sw_reset",
		.reg = NVSW_SN2201_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(4),
		.mode = 0444,
	},
	{
		.label = "reset_fw_reset",
		.reg = NVSW_SN2201_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(5),
		.mode = 0444,
	},
	{
		.label = "reset_swb_wd",
		.reg = NVSW_SN2201_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0444,
	},
	{
		.label = "reset_asic_thermal",
		.reg = NVSW_SN2201_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(7),
		.mode = 0444,
	},
	{
		.label = "reset_system",
		.reg = NVSW_SN2201_RST_CAUSE2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0444,
	},
	{
		.label = "reset_sw_pwr_off",
		.reg = NVSW_SN2201_RST_CAUSE2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0444,
	},
	{
		.label = "reset_cpu_pwr_fail_thermal",
		.reg = NVSW_SN2201_RST_CAUSE2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(4),
		.mode = 0444,
	},
	{
		.label = "reset_reload_bios",
		.reg = NVSW_SN2201_RST_CAUSE2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(5),
		.mode = 0444,
	},
	{
		.label = "reset_ac_pwr_fail",
		.reg = NVSW_SN2201_RST_CAUSE2_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0444,
	},
	{
		.label = "psu1",
		.reg = NVSW_SN2201_PS_PRSNT_STATUS_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0444,
	},
	{
		.label = "psu2",
		.reg = NVSW_SN2201_PS_PRSNT_STATUS_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0444,
	},
};

static struct mlxreg_core_platform_data nvsw_sn2201_regs_io = {
	.data = nvsw_sn2201_io_data,
	.counter = ARRAY_SIZE(nvsw_sn2201_io_data),
};

/* Default watchdog data. */
static struct mlxreg_core_data nvsw_sn2201_wd_data[] = {
	{
		.label = "action",
		.reg = NVSW_SN2201_WD_ACT_OFFSET,
		.mask = GENMASK(7, 1),
		.bit = 0,
	},
	{
		.label = "timeout",
		.reg = NVSW_SN2201_WD_TMR_OFFSET_LSB,
		.mask = 0,
		.health_cntr = NVSW_SN2201_WD_DFLT_TIMEOUT,
	},
	{
		.label = "timeleft",
		.reg = NVSW_SN2201_WD_TMR_OFFSET_LSB,
		.mask = 0,
	},
	{
		.label = "ping",
		.reg = NVSW_SN2201_WD_ACT_OFFSET,
		.mask = GENMASK(7, 1),
		.bit = 0,
	},
	{
		.label = "reset",
		.reg = NVSW_SN2201_RST_CAUSE1_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.bit = 6,
	},
};

static struct mlxreg_core_platform_data nvsw_sn2201_wd = {
	.data = nvsw_sn2201_wd_data,
	.counter = ARRAY_SIZE(nvsw_sn2201_wd_data),
	.version = MLX_WDT_TYPE3,
	.identity = "mlx-wdt-main",
};

static int
nvsw_sn2201_create_static_devices(struct nvsw_sn2201 *nvsw_sn2201,
				  struct mlxreg_hotplug_device *devs,
				  int size)
{
	struct mlxreg_hotplug_device *dev = devs;
	int ret;
	int i;

	/* Create I2C static devices. */
	for (i = 0; i < size; i++, dev++) {
		dev->client = i2c_new_client_device(dev->adapter, dev->brdinfo);
		if (IS_ERR(dev->client)) {
			dev_err(nvsw_sn2201->dev, "Failed to create client %s at bus %d at addr 0x%02x\n",
				dev->brdinfo->type,
				dev->nr, dev->brdinfo->addr);

			dev->adapter = NULL;
			ret = PTR_ERR(dev->client);
			goto fail_create_static_devices;
		}
	}

	return 0;

fail_create_static_devices:
	while (--i >= 0) {
		dev = devs + i;
		i2c_unregister_device(dev->client);
		dev->client = NULL;
		dev->adapter = NULL;
	}
	return ret;
}

static void nvsw_sn2201_destroy_static_devices(struct nvsw_sn2201 *nvsw_sn2201,
					       struct mlxreg_hotplug_device *devs, int size)
{
	struct mlxreg_hotplug_device *dev = devs;
	int i;

	/* Destroy static I2C device for SN2201 static devices. */
	for (i = 0; i < size; i++, dev++) {
		if (dev->client) {
			i2c_unregister_device(dev->client);
			dev->client = NULL;
			i2c_put_adapter(dev->adapter);
			dev->adapter = NULL;
		}
	}
}

static int nvsw_sn2201_config_post_init(struct nvsw_sn2201 *nvsw_sn2201)
{
	struct mlxreg_hotplug_device *sn2201_dev;
	struct i2c_adapter *adap;
	struct device *dev;
	int i, err;

	dev = nvsw_sn2201->dev;
	adap = i2c_get_adapter(nvsw_sn2201->main_mux_deferred_nr);
	if (!adap) {
		dev_err(dev, "Failed to get adapter for bus %d\n",
			nvsw_sn2201->main_mux_deferred_nr);
		return -ENODEV;
	}
	i2c_put_adapter(adap);

	/* Update board info. */
	sn2201_dev = nvsw_sn2201->sn2201_devs;
	for (i = 0; i < nvsw_sn2201->sn2201_devs_num; i++, sn2201_dev++) {
		sn2201_dev->adapter = i2c_get_adapter(sn2201_dev->nr);
		if (!sn2201_dev->adapter)
			return -ENODEV;
		i2c_put_adapter(sn2201_dev->adapter);
	}

	err = nvsw_sn2201_create_static_devices(nvsw_sn2201, nvsw_sn2201->sn2201_devs,
						nvsw_sn2201->sn2201_devs_num);
	if (err)
		dev_err(dev, "Failed to create static devices\n");

	return err;
}

static int nvsw_sn2201_config_init(struct nvsw_sn2201 *nvsw_sn2201, void *regmap)
{
	struct device *dev = nvsw_sn2201->dev;
	int err;

	nvsw_sn2201->io_data = &nvsw_sn2201_regs_io;
	nvsw_sn2201->led_data = &nvsw_sn2201_led;
	nvsw_sn2201->wd_data = &nvsw_sn2201_wd;
	if (nvsw_sn2201->ext_pwr_source)
		nvsw_sn2201->hotplug_data = &nvsw_sn2201_busbar_hotplug;
	else
		nvsw_sn2201->hotplug_data = &nvsw_sn2201_hotplug;

	/* Register IO access driver. */
	if (nvsw_sn2201->io_data) {
		nvsw_sn2201->io_data->regmap = regmap;
		nvsw_sn2201->io_regs =
		platform_device_register_resndata(dev, "mlxreg-io", PLATFORM_DEVID_NONE, NULL, 0,
						  nvsw_sn2201->io_data,
						  sizeof(*nvsw_sn2201->io_data));
		if (IS_ERR(nvsw_sn2201->io_regs)) {
			err = PTR_ERR(nvsw_sn2201->io_regs);
			goto fail_register_io;
		}
	}

	/* Register LED driver. */
	if (nvsw_sn2201->led_data) {
		nvsw_sn2201->led_data->regmap = regmap;
		nvsw_sn2201->led =
		platform_device_register_resndata(dev, "leds-mlxreg", PLATFORM_DEVID_NONE, NULL, 0,
						  nvsw_sn2201->led_data,
						  sizeof(*nvsw_sn2201->led_data));
		if (IS_ERR(nvsw_sn2201->led)) {
			err = PTR_ERR(nvsw_sn2201->led);
			goto fail_register_led;
		}
	}

	/* Register WD driver. */
	if (nvsw_sn2201->wd_data) {
		nvsw_sn2201->wd_data->regmap = regmap;
		nvsw_sn2201->wd =
		platform_device_register_resndata(dev, "mlx-wdt", PLATFORM_DEVID_NONE, NULL, 0,
						  nvsw_sn2201->wd_data,
						  sizeof(*nvsw_sn2201->wd_data));
		if (IS_ERR(nvsw_sn2201->wd)) {
			err = PTR_ERR(nvsw_sn2201->wd);
			goto fail_register_wd;
		}
	}

	/* Register hotplug driver. */
	if (nvsw_sn2201->hotplug_data) {
		nvsw_sn2201->hotplug_data->regmap = regmap;
		nvsw_sn2201->pdev_hotplug =
		platform_device_register_resndata(dev, "mlxreg-hotplug", PLATFORM_DEVID_NONE,
						  nvsw_sn2201_cpld_res,
						  ARRAY_SIZE(nvsw_sn2201_cpld_res),
						  nvsw_sn2201->hotplug_data,
						  sizeof(*nvsw_sn2201->hotplug_data));
		if (IS_ERR(nvsw_sn2201->pdev_hotplug)) {
			err = PTR_ERR(nvsw_sn2201->pdev_hotplug);
			goto fail_register_hotplug;
		}
	}

	return nvsw_sn2201_config_post_init(nvsw_sn2201);

fail_register_hotplug:
	if (nvsw_sn2201->wd)
		platform_device_unregister(nvsw_sn2201->wd);
fail_register_wd:
	if (nvsw_sn2201->led)
		platform_device_unregister(nvsw_sn2201->led);
fail_register_led:
	if (nvsw_sn2201->io_regs)
		platform_device_unregister(nvsw_sn2201->io_regs);
fail_register_io:

	return err;
}

static void nvsw_sn2201_config_exit(struct nvsw_sn2201 *nvsw_sn2201)
{
	/* Unregister hotplug driver. */
	if (nvsw_sn2201->pdev_hotplug)
		platform_device_unregister(nvsw_sn2201->pdev_hotplug);
	/* Unregister WD driver. */
	if (nvsw_sn2201->wd)
		platform_device_unregister(nvsw_sn2201->wd);
	/* Unregister LED driver. */
	if (nvsw_sn2201->led)
		platform_device_unregister(nvsw_sn2201->led);
	/* Unregister IO access driver. */
	if (nvsw_sn2201->io_regs)
		platform_device_unregister(nvsw_sn2201->io_regs);
}

/*
 * Initialization is divided into two parts:
 * - I2C main bus init.
 * - Mux creation and attaching devices to the mux,
 *   which assumes that the main bus is already created.
 * This separation is required for synchronization between these two parts.
 * Completion notify callback is used to make this flow synchronized.
 */
static int nvsw_sn2201_i2c_completion_notify(void *handle, int id)
{
	struct nvsw_sn2201 *nvsw_sn2201 = handle;
	void *regmap;
	int i, err;

	/* Create main mux. */
	nvsw_sn2201->main_mux_devs->adapter = i2c_get_adapter(nvsw_sn2201->main_mux_devs->nr);
	if (!nvsw_sn2201->main_mux_devs->adapter) {
		err = -ENODEV;
		dev_err(nvsw_sn2201->dev, "Failed to get adapter for bus %d\n",
			nvsw_sn2201->cpld_devs->nr);
		goto i2c_get_adapter_main_fail;
	}

	nvsw_sn2201->main_mux_devs_num = ARRAY_SIZE(nvsw_sn2201_main_mux_brdinfo);
	err = nvsw_sn2201_create_static_devices(nvsw_sn2201, nvsw_sn2201->main_mux_devs,
						nvsw_sn2201->main_mux_devs_num);
	if (err) {
		dev_err(nvsw_sn2201->dev, "Failed to create main mux devices\n");
		goto nvsw_sn2201_create_static_devices_fail;
	}

	nvsw_sn2201->cpld_devs->adapter = i2c_get_adapter(nvsw_sn2201->cpld_devs->nr);
	if (!nvsw_sn2201->cpld_devs->adapter) {
		err = -ENODEV;
		dev_err(nvsw_sn2201->dev, "Failed to get adapter for bus %d\n",
			nvsw_sn2201->cpld_devs->nr);
		goto i2c_get_adapter_fail;
	}

	/* Create CPLD device. */
	nvsw_sn2201->cpld_devs->client = i2c_new_dummy_device(nvsw_sn2201->cpld_devs->adapter,
							      NVSW_SN2201_CPLD_I2CADDR);
	if (IS_ERR(nvsw_sn2201->cpld_devs->client)) {
		err = PTR_ERR(nvsw_sn2201->cpld_devs->client);
		dev_err(nvsw_sn2201->dev, "Failed to create %s cpld device at bus %d at addr 0x%02x\n",
			nvsw_sn2201->cpld_devs->brdinfo->type, nvsw_sn2201->cpld_devs->nr,
			nvsw_sn2201->cpld_devs->brdinfo->addr);
		goto i2c_new_dummy_fail;
	}

	regmap = devm_regmap_init_i2c(nvsw_sn2201->cpld_devs->client, &nvsw_sn2201_regmap_conf);
	if (IS_ERR(regmap)) {
		err = PTR_ERR(regmap);
		dev_err(nvsw_sn2201->dev, "Failed to initialise managed register map\n");
		goto devm_regmap_init_i2c_fail;
	}

	/* Set default registers. */
	for (i = 0; i < nvsw_sn2201_regmap_conf.num_reg_defaults; i++) {
		err = regmap_write(regmap, nvsw_sn2201_regmap_default[i].reg,
				   nvsw_sn2201_regmap_default[i].def);
		if (err) {
			dev_err(nvsw_sn2201->dev, "Failed to set register at offset 0x%02x to default value: 0x%02x\n",
				nvsw_sn2201_regmap_default[i].reg,
				nvsw_sn2201_regmap_default[i].def);
			goto regmap_write_fail;
		}
	}

	/* Sync registers with hardware. */
	regcache_mark_dirty(regmap);
	err = regcache_sync(regmap);
	if (err) {
		dev_err(nvsw_sn2201->dev, "Failed to Sync registers with hardware\n");
		goto regcache_sync_fail;
	}

	/* Configure SN2201 board. */
	err = nvsw_sn2201_config_init(nvsw_sn2201, regmap);
	if (err) {
		dev_err(nvsw_sn2201->dev, "Failed to configure board\n");
		goto nvsw_sn2201_config_init_fail;
	}

	return 0;

nvsw_sn2201_config_init_fail:
	nvsw_sn2201_config_exit(nvsw_sn2201);
regcache_sync_fail:
regmap_write_fail:
devm_regmap_init_i2c_fail:
i2c_new_dummy_fail:
	i2c_put_adapter(nvsw_sn2201->cpld_devs->adapter);
	nvsw_sn2201->cpld_devs->adapter = NULL;
i2c_get_adapter_fail:
	/* Destroy SN2201 static I2C devices. */
	nvsw_sn2201_destroy_static_devices(nvsw_sn2201, nvsw_sn2201->sn2201_devs,
					   nvsw_sn2201->sn2201_devs_num);
	/* Destroy main mux device. */
	nvsw_sn2201_destroy_static_devices(nvsw_sn2201, nvsw_sn2201->main_mux_devs,
					   nvsw_sn2201->main_mux_devs_num);
nvsw_sn2201_create_static_devices_fail:
	i2c_put_adapter(nvsw_sn2201->main_mux_devs->adapter);
i2c_get_adapter_main_fail:
	return err;
}

static int nvsw_sn2201_config_pre_init(struct nvsw_sn2201 *nvsw_sn2201)
{
	nvsw_sn2201->i2c_data = &nvsw_sn2201_i2c_data;

	/* Register I2C controller. */
	nvsw_sn2201->i2c_data->handle = nvsw_sn2201;
	nvsw_sn2201->i2c_data->completion_notify = nvsw_sn2201_i2c_completion_notify;
	nvsw_sn2201->pdev_i2c = platform_device_register_resndata(nvsw_sn2201->dev, "i2c_mlxcpld",
								  NVSW_SN2201_MAIN_MUX_NR,
								  nvsw_sn2201_lpc_res,
								  ARRAY_SIZE(nvsw_sn2201_lpc_res),
								  nvsw_sn2201->i2c_data,
								  sizeof(*nvsw_sn2201->i2c_data));
	if (IS_ERR(nvsw_sn2201->pdev_i2c))
		return PTR_ERR(nvsw_sn2201->pdev_i2c);

	return 0;
}

static int nvsw_sn2201_probe(struct platform_device *pdev)
{
	struct nvsw_sn2201 *nvsw_sn2201;
	const char *sku;
	int ret;

	nvsw_sn2201 = devm_kzalloc(&pdev->dev, sizeof(*nvsw_sn2201), GFP_KERNEL);
	if (!nvsw_sn2201)
		return -ENOMEM;

	/* Validate system powering type - only HI168 SKU supports external power. */
	sku = dmi_get_system_info(DMI_PRODUCT_SKU);
	if (sku && !strcmp(sku, "HI168"))
		nvsw_sn2201->ext_pwr_source = true;

	nvsw_sn2201->dev = &pdev->dev;
	platform_set_drvdata(pdev, nvsw_sn2201);
	ret = platform_device_add_resources(pdev, nvsw_sn2201_lpc_io_resources,
				      ARRAY_SIZE(nvsw_sn2201_lpc_io_resources));
	if (ret)
		return ret;

	nvsw_sn2201->main_mux_deferred_nr = NVSW_SN2201_MAIN_MUX_DEFER_NR;
	nvsw_sn2201->main_mux_devs = nvsw_sn2201_main_mux_brdinfo;
	nvsw_sn2201->cpld_devs = nvsw_sn2201_cpld_brdinfo;
	if (nvsw_sn2201->ext_pwr_source) {
		nvsw_sn2201->sn2201_devs = nvsw_sn2201_busbar_static_brdinfo;
		nvsw_sn2201->sn2201_devs_num = ARRAY_SIZE(nvsw_sn2201_busbar_static_brdinfo);
	} else {
		nvsw_sn2201->sn2201_devs = nvsw_sn2201_static_brdinfo;
		nvsw_sn2201->sn2201_devs_num = ARRAY_SIZE(nvsw_sn2201_static_brdinfo);
	}

	return nvsw_sn2201_config_pre_init(nvsw_sn2201);
}

static void nvsw_sn2201_remove(struct platform_device *pdev)
{
	struct nvsw_sn2201 *nvsw_sn2201 = platform_get_drvdata(pdev);

	/* Unregister underlying drivers. */
	nvsw_sn2201_config_exit(nvsw_sn2201);

	/* Destroy SN2201 static I2C devices. */
	nvsw_sn2201_destroy_static_devices(nvsw_sn2201,
					   nvsw_sn2201->sn2201_devs,
					   nvsw_sn2201->sn2201_devs_num);

	i2c_put_adapter(nvsw_sn2201->cpld_devs->adapter);
	nvsw_sn2201->cpld_devs->adapter = NULL;
	/* Destroy main mux device. */
	nvsw_sn2201_destroy_static_devices(nvsw_sn2201,
					   nvsw_sn2201->main_mux_devs,
					   nvsw_sn2201->main_mux_devs_num);

	/* Unregister I2C controller. */
	if (nvsw_sn2201->pdev_i2c)
		platform_device_unregister(nvsw_sn2201->pdev_i2c);
}

static const struct acpi_device_id nvsw_sn2201_acpi_ids[] = {
	{"NVSN2201", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, nvsw_sn2201_acpi_ids);

static struct platform_driver nvsw_sn2201_driver = {
	.probe = nvsw_sn2201_probe,
	.remove = nvsw_sn2201_remove,
	.driver = {
		.name = "nvsw-sn2201",
	.acpi_match_table = nvsw_sn2201_acpi_ids,
	},
};

module_platform_driver(nvsw_sn2201_driver);

MODULE_AUTHOR("Nvidia");
MODULE_DESCRIPTION("Nvidia sn2201 platform driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:nvsw-sn2201");
