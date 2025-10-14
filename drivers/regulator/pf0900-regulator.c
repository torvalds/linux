// SPDX-License-Identifier: GPL-2.0
// Copyright 2025 NXP.
// NXP PF0900 pmic driver

#include <linux/bitfield.h>
#include <linux/crc8.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

enum pf0900_regulators {
	PF0900_SW1 = 0,
	PF0900_SW2,
	PF0900_SW3,
	PF0900_SW4,
	PF0900_SW5,
	PF0900_LDO1,
	PF0900_LDO2,
	PF0900_LDO3,
	PF0900_VAON,
	PF0900_REGULATOR_CNT,
};

enum {
	PF0900_DVS_LEVEL_RUN = 0,
	PF0900_DVS_LEVEL_STANDBY,
	PF0900_DVS_LEVEL_MAX,
};


#define PF0900_VAON_VOLTAGE_NUM 0x03
#define PF0900_SW_VOLTAGE_NUM   0x100
#define PF0900_LDO_VOLTAGE_NUM  0x20

#define REGU_SW_CNT             0x5
#define REGU_LDO_VAON_CNT       0x4

enum {
	PF0900_REG_DEV_ID	    = 0x00,
	PF0900_REG_DEV_FAM	    = 0x01,
	PF0900_REG_REV_ID	    = 0x02,
	PF0900_REG_PROG_ID1	    = 0x03,
	PF0900_REG_PROG_ID2	    = 0x04,
	PF0900_REG_SYSTEM_INT	    = 0x05,
	PF0900_REG_STATUS1_INT	    = 0x06,
	PF0900_REG_STATUS1_MSK      = 0x07,
	PF0900_REG_STATUS1_SNS	    = 0x08,
	PF0900_REG_STATUS2_INT      = 0x09,
	PF0900_REG_STATUS2_MSK      = 0x0A,
	PF0900_REG_STATUS2_SNS	    = 0x0B,
	PF0900_REG_STATUS3_INT      = 0x0C,
	PF0900_REG_STATUS3_MSK      = 0x0D,
	PF0900_REG_SW_MODE_INT      = 0x0E,
	PF0900_REG_SW_MODE_MSK      = 0x0F,
	PF0900_REG_SW_ILIM_INT      = 0x10,
	PF0900_REG_SW_ILIM_MSK      = 0x11,
	PF0900_REG_SW_ILIM_SNS      = 0x12,
	PF0900_REG_LDO_ILIM_INT     = 0x13,
	PF0900_REG_LDO_ILIM_MSK     = 0x14,
	PF0900_REG_LDO_ILIM_SNS     = 0x15,
	PF0900_REG_SW_UV_INT        = 0x16,
	PF0900_REG_SW_UV_MSK        = 0x17,
	PF0900_REG_SW_UV_SNS        = 0x18,
	PF0900_REG_SW_OV_INT        = 0x19,
	PF0900_REG_SW_OV_MSK        = 0x1A,
	PF0900_REG_SW_OV_SNS        = 0x1B,
	PF0900_REG_LDO_UV_INT       = 0x1C,
	PF0900_REG_LDO_UV_MSK       = 0x1D,
	PF0900_REG_LDO_UV_SNS       = 0x1E,
	PF0900_REG_LDO_OV_INT       = 0x1F,
	PF0900_REG_LDO_OV_MSK       = 0x20,
	PF0900_REG_LDO_OV_SNS       = 0x21,
	PF0900_REG_PWRON_INT        = 0x22,
	PF0900_REG_IO_INT           = 0x24,
	PF0900_REG_IO_MSK           = 0x25,
	PF0900_REG_IO_SNS           = 0x26,
	PF0900_REG_IOSHORT_SNS      = 0x27,
	PF0900_REG_ABIST_OV1        = 0x28,
	PF0900_REG_ABIST_OV2        = 0x29,
	PF0900_REG_ABIST_UV1        = 0x2A,
	PF0900_REG_ABIST_UV2        = 0x2B,
	PF0900_REG_ABIST_IO         = 0x2C,
	PF0900_REG_TEST_FLAGS       = 0x2D,
	PF0900_REG_HFAULT_FLAGS     = 0x2E,
	PF0900_REG_FAULT_FLAGS      = 0x2F,
	PF0900_REG_FS0B_CFG         = 0x30,
	PF0900_REG_FCCU_CFG         = 0x31,
	PF0900_REG_RSTB_CFG1        = 0x32,
	PF0900_REG_SYSTEM_CMD       = 0x33,
	PF0900_REG_FS0B_CMD         = 0x34,
	PF0900_REG_SECURE_WR1       = 0x35,
	PF0900_REG_SECURE_WR2       = 0x36,
	PF0900_REG_VMON_CFG1        = 0x37,
	PF0900_REG_SYS_CFG1         = 0x38,
	PF0900_REG_GPO_CFG          = 0x39,
	PF0900_REG_GPO_CTRL         = 0x3A,
	PF0900_REG_PWRUP_CFG        = 0x3B,
	PF0900_REG_RSTB_PWRUP       = 0x3C,
	PF0900_REG_GPIO1_PWRUP      = 0x3D,
	PF0900_REG_GPIO2_PWRUP      = 0x3E,
	PF0900_REG_GPIO3_PWRUP      = 0x3F,
	PF0900_REG_GPIO4_PWRUP      = 0x40,
	PF0900_REG_VMON1_PWRUP      = 0x41,
	PF0900_REG_VMON2_PWRUP      = 0x42,
	PF0900_REG_SW1_PWRUP        = 0x43,
	PF0900_REG_SW2_PWRUP        = 0x44,
	PF0900_REG_SW3_PWRUP        = 0x45,
	PF0900_REG_SW4_PWRUP        = 0x46,
	PF0900_REG_SW5_PWRUP        = 0x47,
	PF0900_REG_LDO1_PWRUP       = 0x48,
	PF0900_REG_LDO2_PWRUP       = 0x49,
	PF0900_REG_LDO3_PWRUP       = 0x4A,
	PF0900_REG_VAON_PWRUP       = 0x4B,
	PF0900_REG_FREQ_CTRL        = 0x4C,
	PF0900_REG_PWRON_CFG        = 0x4D,
	PF0900_REG_WD_CTRL1         = 0x4E,
	PF0900_REG_WD_CTRL2         = 0x4F,
	PF0900_REG_WD_CFG1          = 0x50,
	PF0900_REG_WD_CFG2          = 0x51,
	PF0900_REG_WD_CNT1          = 0x52,
	PF0900_REG_WD_CNT2          = 0x53,
	PF0900_REG_FAULT_CFG        = 0x54,
	PF0900_REG_FAULT_CNT        = 0x55,
	PF0900_REG_DFS_CNT          = 0x56,
	PF0900_REG_AMUX_CFG         = 0x57,
	PF0900_REG_VMON1_RUN_CFG    = 0x58,
	PF0900_REG_VMON1_STBY_CFG   = 0x59,
	PF0900_REG_VMON1_CTRL       = 0x5A,
	PF0900_REG_VMON2_RUN_CFG    = 0x5B,
	PF0900_REG_VMON2_STBY_CFG   = 0x5C,
	PF0900_REG_VMON2_CTRL       = 0x5D,
	PF0900_REG_SW1_VRUN         = 0x5E,
	PF0900_REG_SW1_VSTBY        = 0x5F,
	PF0900_REG_SW1_MODE         = 0x60,
	PF0900_REG_SW1_CFG1         = 0x61,
	PF0900_REG_SW1_CFG2         = 0x62,
	PF0900_REG_SW2_VRUN         = 0x63,
	PF0900_REG_SW2_VSTBY        = 0x64,
	PF0900_REG_SW2_MODE         = 0x65,
	PF0900_REG_SW2_CFG1         = 0x66,
	PF0900_REG_SW2_CFG2         = 0x67,
	PF0900_REG_SW3_VRUN         = 0x68,
	PF0900_REG_SW3_VSTBY        = 0x69,
	PF0900_REG_SW3_MODE         = 0x6A,
	PF0900_REG_SW3_CFG1         = 0x6B,
	PF0900_REG_SW3_CFG2         = 0x6C,
	PF0900_REG_SW4_VRUN         = 0x6D,
	PF0900_REG_SW4_VSTBY        = 0x6E,
	PF0900_REG_SW4_MODE         = 0x6F,
	PF0900_REG_SW4_CFG1         = 0x70,
	PF0900_REG_SW4_CFG2         = 0x71,
	PF0900_REG_SW5_VRUN         = 0x72,
	PF0900_REG_SW5_VSTBY        = 0x73,
	PF0900_REG_SW5_MODE         = 0x74,
	PF0900_REG_SW5_CFG1         = 0x75,
	PF0900_REG_SW5_CFG2         = 0x76,
	PF0900_REG_LDO1_RUN         = 0x77,
	PF0900_REG_LDO1_STBY        = 0x78,
	PF0900_REG_LDO1_CFG2        = 0x79,
	PF0900_REG_LDO2_RUN         = 0x7A,
	PF0900_REG_LDO2_STBY        = 0x7B,
	PF0900_REG_LDO2_CFG2        = 0x7C,
	PF0900_REG_LDO3_RUN         = 0x7D,
	PF0900_REG_LDO3_STBY        = 0x7E,
	PF0900_REG_LDO3_CFG2        = 0x7F,
	PF0900_REG_VAON_CFG1        = 0x80,
	PF0900_REG_VAON_CFG2        = 0x81,
	PF0900_REG_SYS_DIAG         = 0x82,
	PF0900_MAX_REGISTER,
};

/* PF0900 SW MODE */
#define SW_RUN_MODE_OFF                 0x00
#define SW_RUN_MODE_PWM                 0x01
#define SW_RUN_MODE_PFM                 0x02
#define SW_STBY_MODE_OFF                0x00
#define SW_STBY_MODE_PWM                0x04
#define SW_STBY_MODE_PFM                0x08

/* PF0900 SW MODE MASK */
#define SW_RUN_MODE_MASK                GENMASK(1, 0)
#define SW_STBY_MODE_MASK               GENMASK(3, 2)

/* PF0900 SW VRUN/VSTBY MASK */
#define PF0900_SW_VOL_MASK              GENMASK(7, 0)

/* PF0900_REG_VAON_CFG1 bits */
#define PF0900_VAON_1P8V                0x01

#define PF0900_VAON_MASK                GENMASK(1, 0)

/* PF0900_REG_SWX_CFG1 MASK */
#define PF0900_SW_DVS_MASK              GENMASK(4, 3)

/* PF0900_REG_LDO_RUN MASK */
#define VLDO_RUN_MASK                   GENMASK(4, 0)
#define LDO_RUN_EN_MASK                 BIT(5)

/* PF0900_REG_STATUS1_INT bits */
#define PF0900_IRQ_PWRUP                BIT(3)

/* PF0900_REG_ILIM_INT bits */
#define PF0900_IRQ_SW1_IL               BIT(0)
#define PF0900_IRQ_SW2_IL               BIT(1)
#define PF0900_IRQ_SW3_IL               BIT(2)
#define PF0900_IRQ_SW4_IL               BIT(3)
#define PF0900_IRQ_SW5_IL               BIT(4)

#define PF0900_IRQ_LDO1_IL              BIT(0)
#define PF0900_IRQ_LDO2_IL              BIT(1)
#define PF0900_IRQ_LDO3_IL              BIT(2)

/* PF0900_REG_UV_INT bits */
#define PF0900_IRQ_SW1_UV               BIT(0)
#define PF0900_IRQ_SW2_UV               BIT(1)
#define PF0900_IRQ_SW3_UV               BIT(2)
#define PF0900_IRQ_SW4_UV               BIT(3)
#define PF0900_IRQ_SW5_UV               BIT(4)

#define PF0900_IRQ_LDO1_UV              BIT(0)
#define PF0900_IRQ_LDO2_UV              BIT(1)
#define PF0900_IRQ_LDO3_UV              BIT(2)
#define PF0900_IRQ_VAON_UV              BIT(3)

/* PF0900_REG_OV_INT bits */
#define PF0900_IRQ_SW1_OV               BIT(0)
#define PF0900_IRQ_SW2_OV               BIT(1)
#define PF0900_IRQ_SW3_OV               BIT(2)
#define PF0900_IRQ_SW4_OV               BIT(3)
#define PF0900_IRQ_SW5_OV               BIT(4)

#define PF0900_IRQ_LDO1_OV              BIT(0)
#define PF0900_IRQ_LDO2_OV              BIT(1)
#define PF0900_IRQ_LDO3_OV              BIT(2)
#define PF0900_IRQ_VAON_OV              BIT(3)

struct pf0900_regulator_desc {
	struct regulator_desc desc;
	unsigned int suspend_enable_mask;
	unsigned int suspend_voltage_reg;
	unsigned int suspend_voltage_cache;
};

struct pf0900_drvdata {
	const struct pf0900_regulator_desc *desc;
	unsigned int rcnt;
};

struct pf0900 {
	struct device *dev;
	struct regmap *regmap;
	const struct pf0900_drvdata *drvdata;
	struct regulator_dev *rdevs[PF0900_REGULATOR_CNT];
	int irq;
	unsigned short addr;
	bool crc_en;
};

enum pf0900_regulator_type {
	PF0900_SW = 0,
	PF0900_LDO,
};

#define PF0900_REGU_IRQ(_reg, _type, _event)	\
	{					\
		.reg = _reg,			\
		.type = _type,			\
		.event = _event,		\
	}

struct pf0900_regulator_irq {
	unsigned int  reg;
	unsigned int  type;
	unsigned int  event;
};

static const struct regmap_range pf0900_range = {
	.range_min = PF0900_REG_DEV_ID,
	.range_max = PF0900_REG_SYS_DIAG,
};

static const struct regmap_access_table pf0900_volatile_regs = {
	.yes_ranges = &pf0900_range,
	.n_yes_ranges = 1,
};

static const struct regmap_config pf0900_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &pf0900_volatile_regs,
	.max_register = PF0900_MAX_REGISTER - 1,
	.cache_type = REGCACHE_MAPLE,
};

static uint8_t crc8_j1850(unsigned short addr, unsigned int reg,
			  unsigned int val)
{
	uint8_t crcBuf[3];
	uint8_t t_crc;
	uint8_t i, j;

	crcBuf[0] = addr;
	crcBuf[1] = reg;
	crcBuf[2] = val;
	t_crc = 0xFF;

	/*
	 * The CRC calculation is based on the standard CRC-8-SAE as
	 * defined in the SAE-J1850 specification with the following
	 * characteristics.
	 * Polynomial = 0x1D
	 * Initial Value = 0xFF
	 * The CRC byte is calculated by shifting 24-bit data through
	 * the CRC polynomial.The 24-bits package is built as follows:
	 * DEVICE_ADDR[b8] + REGISTER_ADDR [b8] +DATA[b8]
	 * The DEVICE_ADDR is calculated as the 7-bit slave address
	 * shifted left one space plus the corresponding read/write bit.
	 * (7Bit Address [b7] << 1 ) + R/W = DEVICE_ADDR[b8]
	 */
	for (i = 0; i < sizeof(crcBuf); i++) {
		t_crc ^= crcBuf[i];
		for (j = 0; j < 8; j++) {
			if ((t_crc & 0x80) != 0) {
				t_crc <<= 1;
				t_crc ^= 0x1D;
			} else {
				t_crc <<= 1;
			}
		}
	}

	return t_crc;
}

static int pf0900_regmap_read(void *context, unsigned int reg,
			      unsigned int *val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct pf0900 *pf0900 = dev_get_drvdata(dev);
	int ret;
	u8 crc;

	if (!pf0900 || !pf0900->dev)
		return -EINVAL;

	if (reg >= PF0900_MAX_REGISTER) {
		dev_err(pf0900->dev, "Invalid register address: 0x%x\n", reg);
		return -EINVAL;
	}

	if (pf0900->crc_en) {
		ret = i2c_smbus_read_word_data(i2c, reg);
		if (ret < 0) {
			dev_err(pf0900->dev, "Read error at reg=0x%x: %d\n", reg, ret);
			return ret;
		}

		*val = (u16)ret;
		crc = crc8_j1850(pf0900->addr << 1 | 0x1, reg, FIELD_GET(GENMASK(7, 0), *val));
		if (crc != FIELD_GET(GENMASK(15, 8), *val)) {
			dev_err(pf0900->dev, "Crc check error!\n");
			return -EINVAL;
		}
		*val = FIELD_GET(GENMASK(7, 0), *val);
	} else {
		ret = i2c_smbus_read_byte_data(i2c, reg);
		if (ret < 0) {
			dev_err(pf0900->dev, "Read error at reg=0x%x: %d\n", reg, ret);
			return ret;
		}
		*val = ret;
	}

	return 0;
}

static int pf0900_regmap_write(void *context, unsigned int reg,
			       unsigned int val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct pf0900 *pf0900 = dev_get_drvdata(dev);
	uint8_t data[2];
	int ret;

	if (!pf0900 || !pf0900->dev)
		return -EINVAL;

	if (reg >= PF0900_MAX_REGISTER) {
		dev_err(pf0900->dev, "Invalid register address: 0x%x\n", reg);
		return -EINVAL;
	}

	data[0] = val;
	if (pf0900->crc_en) {
		/* Get CRC */
		data[1] = crc8_j1850(pf0900->addr << 1, reg, data[0]);
		val = FIELD_PREP(GENMASK(15, 8), data[1]) | data[0];
		ret = i2c_smbus_write_word_data(i2c, reg, val);
	} else {
		ret = i2c_smbus_write_byte_data(i2c, reg, data[0]);
	}

	if (ret) {
		dev_err(pf0900->dev, "Write reg=0x%x error!\n", reg);
		return ret;
	}

	return 0;
}

static int pf0900_suspend_enable(struct regulator_dev *rdev)
{
	struct pf0900_regulator_desc *rdata = rdev_get_drvdata(rdev);
	struct regmap *rmap = rdev_get_regmap(rdev);

	return regmap_update_bits(rmap, rdata->desc.enable_reg,
				  rdata->suspend_enable_mask, SW_STBY_MODE_PFM);
}

static int pf0900_suspend_disable(struct regulator_dev *rdev)
{
	struct pf0900_regulator_desc *rdata = rdev_get_drvdata(rdev);
	struct regmap *rmap = rdev_get_regmap(rdev);

	return regmap_update_bits(rmap, rdata->desc.enable_reg,
				  rdata->suspend_enable_mask, SW_STBY_MODE_OFF);
}

static int pf0900_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct pf0900_regulator_desc *rdata = rdev_get_drvdata(rdev);
	struct regmap *rmap = rdev_get_regmap(rdev);
	int ret;

	if (rdata->suspend_voltage_cache == uV)
		return 0;

	ret = regulator_map_voltage_iterate(rdev, uV, uV);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev), "failed to map %i uV\n", uV);
		return ret;
	}

	dev_dbg(rdev_get_dev(rdev), "uV: %i, reg: 0x%x, msk: 0x%x, val: 0x%x\n",
		uV, rdata->suspend_voltage_reg, rdata->desc.vsel_mask, ret);
	ret = regmap_update_bits(rmap, rdata->suspend_voltage_reg,
				 rdata->desc.vsel_mask, ret);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev), "failed to set %i uV\n", uV);
		return ret;
	}

	rdata->suspend_voltage_cache = uV;

	return 0;
}

static const struct regmap_bus pf0900_regmap_bus = {
	.reg_read = pf0900_regmap_read,
	.reg_write = pf0900_regmap_write,
};

static const struct regulator_ops pf0900_avon_regulator_ops = {
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops pf0900_dvs_sw_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay	= regulator_set_ramp_delay_regmap,
	.set_suspend_enable = pf0900_suspend_enable,
	.set_suspend_disable = pf0900_suspend_disable,
	.set_suspend_voltage = pf0900_set_suspend_voltage,
};

static const struct regulator_ops pf0900_ldo_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

/*
 * SW1/2/3/4/5
 * SW1_DVS[1:0] SW1 DVS ramp rate setting
 * 00: 15.6mV/8usec
 * 01: 15.6mV/4usec
 * 10: 15.6mV/2usec
 * 11: 15.6mV/1usec
 */
static const unsigned int pf0900_dvs_sw_ramp_table[] = {
	1950, 3900, 7800, 15600
};

/* VAON 1.8V, 3.0V, or 3.3V */
static const int pf0900_vaon_voltages[] = {
	0, 1800000, 3000000, 3300000,
};

/*
 * SW1 0.5V to 3.3V
 * 0.5V to 1.35V (6.25mV step)
 * 1.8V to 2.5V (125mV step)
 * 2.8V to 3.3V (250mV step)
 */
static const struct linear_range pf0900_dvs_sw1_volts[] = {
	REGULATOR_LINEAR_RANGE(0,        0x00, 0x08, 0),
	REGULATOR_LINEAR_RANGE(500000,   0x09, 0x91, 6250),
	REGULATOR_LINEAR_RANGE(0,        0x92, 0x9E, 0),
	REGULATOR_LINEAR_RANGE(1500000,  0x9F, 0x9F, 0),
	REGULATOR_LINEAR_RANGE(1800000,  0xA0, 0xD8, 12500),
	REGULATOR_LINEAR_RANGE(0,        0xD9, 0xDF, 0),
	REGULATOR_LINEAR_RANGE(2800000,  0xE0, 0xF4, 25000),
	REGULATOR_LINEAR_RANGE(0,        0xF5, 0xFF, 0),
};

/*
 * SW2/3/4/5 0.3V to 3.3V
 * 0.45V to 1.35V (6.25mV step)
 * 1.8V to 2.5V (125mV step)
 * 2.8V to 3.3V (250mV step)
 */
static const struct linear_range pf0900_dvs_sw2345_volts[] = {
	REGULATOR_LINEAR_RANGE(300000,   0x00, 0x00, 0),
	REGULATOR_LINEAR_RANGE(450000,   0x01, 0x91, 6250),
	REGULATOR_LINEAR_RANGE(0,        0x92, 0x9E, 0),
	REGULATOR_LINEAR_RANGE(1500000,  0x9F, 0x9F, 0),
	REGULATOR_LINEAR_RANGE(1800000,  0xA0, 0xD8, 12500),
	REGULATOR_LINEAR_RANGE(0,        0xD9, 0xDF, 0),
	REGULATOR_LINEAR_RANGE(2800000,  0xE0, 0xF4, 25000),
	REGULATOR_LINEAR_RANGE(0,        0xF5, 0xFF, 0),
};

/*
 * LDO1
 * 0.75V to 3.3V
 */
static const struct linear_range pf0900_ldo1_volts[] = {
	REGULATOR_LINEAR_RANGE(750000,   0x00, 0x0F, 50000),
	REGULATOR_LINEAR_RANGE(1800000,  0x10, 0x1F, 100000),
};

/*
 * LDO2/3
 * 0.65V to 3.3V (50mV step)
 */
static const struct linear_range pf0900_ldo23_volts[] = {
	REGULATOR_LINEAR_RANGE(650000,   0x00, 0x0D, 50000),
	REGULATOR_LINEAR_RANGE(1400000,  0x0E, 0x0F, 100000),
	REGULATOR_LINEAR_RANGE(1800000,  0x10, 0x1F, 100000),
};

static const struct pf0900_regulator_desc pf0900_regulators[] = {
	{
		.desc = {
			.name = "sw1",
			.of_match = of_match_ptr("sw1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF0900_SW1,
			.ops = &pf0900_dvs_sw_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF0900_SW_VOLTAGE_NUM,
			.linear_ranges = pf0900_dvs_sw1_volts,
			.n_linear_ranges = ARRAY_SIZE(pf0900_dvs_sw1_volts),
			.vsel_reg = PF0900_REG_SW1_VRUN,
			.vsel_mask = PF0900_SW_VOL_MASK,
			.enable_reg = PF0900_REG_SW1_MODE,
			.enable_mask = SW_RUN_MODE_MASK,
			.enable_val = SW_RUN_MODE_PWM,
			.ramp_reg = PF0900_REG_SW1_CFG1,
			.ramp_mask = PF0900_SW_DVS_MASK,
			.ramp_delay_table = pf0900_dvs_sw_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pf0900_dvs_sw_ramp_table),
			.owner = THIS_MODULE,
		},
		.suspend_enable_mask = SW_STBY_MODE_MASK,
		.suspend_voltage_reg = PF0900_REG_SW1_VSTBY,
	},
	{
		.desc = {
			.name = "sw2",
			.of_match = of_match_ptr("sw2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF0900_SW2,
			.ops = &pf0900_dvs_sw_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF0900_SW_VOLTAGE_NUM,
			.linear_ranges = pf0900_dvs_sw2345_volts,
			.n_linear_ranges = ARRAY_SIZE(pf0900_dvs_sw2345_volts),
			.vsel_reg = PF0900_REG_SW2_VRUN,
			.vsel_mask = PF0900_SW_VOL_MASK,
			.enable_reg = PF0900_REG_SW2_MODE,
			.enable_mask = SW_RUN_MODE_MASK,
			.enable_val = SW_RUN_MODE_PWM,
			.ramp_reg = PF0900_REG_SW2_CFG1,
			.ramp_mask = PF0900_SW_DVS_MASK,
			.ramp_delay_table = pf0900_dvs_sw_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pf0900_dvs_sw_ramp_table),
			.owner = THIS_MODULE,
		},
		.suspend_enable_mask = SW_STBY_MODE_MASK,
		.suspend_voltage_reg = PF0900_REG_SW2_VSTBY,
	},
	{
		.desc = {
			.name = "sw3",
			.of_match = of_match_ptr("sw3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF0900_SW3,
			.ops = &pf0900_dvs_sw_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF0900_SW_VOLTAGE_NUM,
			.linear_ranges = pf0900_dvs_sw2345_volts,
			.n_linear_ranges = ARRAY_SIZE(pf0900_dvs_sw2345_volts),
			.vsel_reg = PF0900_REG_SW3_VRUN,
			.vsel_mask = PF0900_SW_VOL_MASK,
			.enable_reg = PF0900_REG_SW3_MODE,
			.enable_mask = SW_RUN_MODE_MASK,
			.enable_val = SW_RUN_MODE_PWM,
			.ramp_reg = PF0900_REG_SW3_CFG1,
			.ramp_mask = PF0900_SW_DVS_MASK,
			.ramp_delay_table = pf0900_dvs_sw_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pf0900_dvs_sw_ramp_table),
			.owner = THIS_MODULE,
		},
		.suspend_enable_mask = SW_STBY_MODE_MASK,
		.suspend_voltage_reg = PF0900_REG_SW3_VSTBY,
	},
	{
		.desc = {
			.name = "sw4",
			.of_match = of_match_ptr("sw4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF0900_SW4,
			.ops = &pf0900_dvs_sw_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF0900_SW_VOLTAGE_NUM,
			.linear_ranges = pf0900_dvs_sw2345_volts,
			.n_linear_ranges = ARRAY_SIZE(pf0900_dvs_sw2345_volts),
			.vsel_reg = PF0900_REG_SW4_VRUN,
			.vsel_mask = PF0900_SW_VOL_MASK,
			.enable_reg = PF0900_REG_SW4_MODE,
			.enable_mask = SW_RUN_MODE_MASK,
			.enable_val = SW_RUN_MODE_PWM,
			.ramp_reg = PF0900_REG_SW4_CFG1,
			.ramp_mask = PF0900_SW_DVS_MASK,
			.ramp_delay_table = pf0900_dvs_sw_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pf0900_dvs_sw_ramp_table),
			.owner = THIS_MODULE,
		},
		.suspend_enable_mask = SW_STBY_MODE_MASK,
		.suspend_voltage_reg = PF0900_REG_SW4_VSTBY,
	},
	{
		.desc = {
			.name = "sw5",
			.of_match = of_match_ptr("sw5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF0900_SW5,
			.ops = &pf0900_dvs_sw_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF0900_SW_VOLTAGE_NUM,
			.linear_ranges = pf0900_dvs_sw2345_volts,
			.n_linear_ranges = ARRAY_SIZE(pf0900_dvs_sw2345_volts),
			.vsel_reg = PF0900_REG_SW5_VRUN,
			.vsel_mask = PF0900_SW_VOL_MASK,
			.enable_reg = PF0900_REG_SW5_MODE,
			.enable_mask = SW_RUN_MODE_MASK,
			.enable_val = SW_RUN_MODE_PWM,
			.ramp_reg = PF0900_REG_SW5_CFG1,
			.ramp_mask = PF0900_SW_DVS_MASK,
			.ramp_delay_table = pf0900_dvs_sw_ramp_table,
			.n_ramp_values = ARRAY_SIZE(pf0900_dvs_sw_ramp_table),
			.owner = THIS_MODULE,
		},
		.suspend_enable_mask = SW_STBY_MODE_MASK,
		.suspend_voltage_reg = PF0900_REG_SW5_VSTBY,
	},
	{
		.desc = {
			.name = "ldo1",
			.of_match = of_match_ptr("ldo1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF0900_LDO1,
			.ops = &pf0900_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF0900_LDO_VOLTAGE_NUM,
			.linear_ranges = pf0900_ldo1_volts,
			.n_linear_ranges = ARRAY_SIZE(pf0900_ldo1_volts),
			.vsel_reg = PF0900_REG_LDO1_RUN,
			.vsel_mask = VLDO_RUN_MASK,
			.enable_reg = PF0900_REG_LDO1_RUN,
			.enable_mask = LDO_RUN_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo2",
			.of_match = of_match_ptr("ldo2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF0900_LDO2,
			.ops = &pf0900_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF0900_LDO_VOLTAGE_NUM,
			.linear_ranges = pf0900_ldo23_volts,
			.n_linear_ranges = ARRAY_SIZE(pf0900_ldo23_volts),
			.vsel_reg = PF0900_REG_LDO2_RUN,
			.vsel_mask = VLDO_RUN_MASK,
			.enable_reg = PF0900_REG_LDO2_RUN,
			.enable_mask = LDO_RUN_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "ldo3",
			.of_match = of_match_ptr("ldo3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF0900_LDO3,
			.ops = &pf0900_ldo_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF0900_LDO_VOLTAGE_NUM,
			.linear_ranges = pf0900_ldo23_volts,
			.n_linear_ranges = ARRAY_SIZE(pf0900_ldo23_volts),
			.vsel_reg = PF0900_REG_LDO3_RUN,
			.vsel_mask = VLDO_RUN_MASK,
			.enable_reg = PF0900_REG_LDO3_RUN,
			.enable_mask = LDO_RUN_EN_MASK,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "vaon",
			.of_match = of_match_ptr("vaon"),
			.regulators_node = of_match_ptr("regulators"),
			.id = PF0900_VAON,
			.ops = &pf0900_avon_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = PF0900_VAON_VOLTAGE_NUM,
			.volt_table = pf0900_vaon_voltages,
			.enable_reg = PF0900_REG_VAON_CFG1,
			.enable_mask = PF0900_VAON_MASK,
			.enable_val = PF0900_VAON_1P8V,
			.vsel_reg = PF0900_REG_VAON_CFG1,
			.vsel_mask = PF0900_VAON_MASK,
			.owner = THIS_MODULE,
		},
	},
};

struct pf0900_regulator_irq regu_irqs[] = {
	PF0900_REGU_IRQ(PF0900_REG_SW_ILIM_INT, PF0900_SW, REGULATOR_ERROR_OVER_CURRENT_WARN),
	PF0900_REGU_IRQ(PF0900_REG_LDO_ILIM_INT, PF0900_LDO, REGULATOR_ERROR_OVER_CURRENT_WARN),
	PF0900_REGU_IRQ(PF0900_REG_SW_UV_INT, PF0900_SW, REGULATOR_ERROR_UNDER_VOLTAGE_WARN),
	PF0900_REGU_IRQ(PF0900_REG_LDO_UV_INT, PF0900_LDO, REGULATOR_ERROR_UNDER_VOLTAGE_WARN),
	PF0900_REGU_IRQ(PF0900_REG_SW_OV_INT, PF0900_SW, REGULATOR_ERROR_OVER_VOLTAGE_WARN),
	PF0900_REGU_IRQ(PF0900_REG_LDO_OV_INT, PF0900_LDO, REGULATOR_ERROR_OVER_VOLTAGE_WARN),
};

static irqreturn_t pf0900_irq_handler(int irq, void *data)
{
	unsigned int val, regu, i, index;
	struct pf0900 *pf0900 = data;
	int ret;

	for (i = 0; i < ARRAY_SIZE(regu_irqs); i++) {
		ret = regmap_read(pf0900->regmap, regu_irqs[i].reg, &val);
		if (ret < 0) {
			dev_err(pf0900->dev, "Failed to read %d\n", ret);
			return IRQ_NONE;
		}
		if (val) {
			ret = regmap_write_bits(pf0900->regmap, regu_irqs[i].reg, val, val);
			if (ret < 0) {
				dev_err(pf0900->dev, "Failed to update %d\n", ret);
				return IRQ_NONE;
			}

			if (regu_irqs[i].type == PF0900_SW) {
				for (index = 0; index < REGU_SW_CNT; index++) {
					if (val & BIT(index)) {
						regu = (enum pf0900_regulators)index;
						regulator_notifier_call_chain(pf0900->rdevs[regu],
									      regu_irqs[i].event,
									      NULL);
					}
				}
			} else if (regu_irqs[i].type == PF0900_LDO) {
				for (index = 0; index < REGU_LDO_VAON_CNT; index++) {
					if (val & BIT(index)) {
						regu = (enum pf0900_regulators)index + PF0900_LDO1;
						regulator_notifier_call_chain(pf0900->rdevs[regu],
									      regu_irqs[i].event,
									      NULL);
					}
				}
			}
		}
	}

	return IRQ_HANDLED;
}

static int pf0900_i2c_probe(struct i2c_client *i2c)
{
	const struct pf0900_regulator_desc *regulator_desc;
	const struct pf0900_drvdata *drvdata = NULL;
	struct device_node *np = i2c->dev.of_node;
	unsigned int device_id, device_fam, i;
	struct regulator_config config = { };
	struct pf0900 *pf0900;
	int ret;

	if (!i2c->irq)
		return dev_err_probe(&i2c->dev, -EINVAL, "No IRQ configured?\n");

	pf0900 = devm_kzalloc(&i2c->dev, sizeof(struct pf0900), GFP_KERNEL);
	if (!pf0900)
		return -ENOMEM;

	drvdata = device_get_match_data(&i2c->dev);
	if (!drvdata)
		return dev_err_probe(&i2c->dev, -EINVAL, "unable to find driver data\n");

	regulator_desc = drvdata->desc;
	pf0900->drvdata = drvdata;
	pf0900->crc_en = of_property_read_bool(np, "nxp,i2c-crc-enable");
	pf0900->irq = i2c->irq;
	pf0900->dev = &i2c->dev;
	pf0900->addr = i2c->addr;

	dev_set_drvdata(&i2c->dev, pf0900);

	pf0900->regmap = devm_regmap_init(&i2c->dev, &pf0900_regmap_bus, &i2c->dev,
					       &pf0900_regmap_config);
	if (IS_ERR(pf0900->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(pf0900->regmap),
				     "regmap initialization failed\n");
	ret = regmap_read(pf0900->regmap, PF0900_REG_DEV_ID, &device_id);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Read device id error\n");

	ret = regmap_read(pf0900->regmap, PF0900_REG_DEV_FAM, &device_fam);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Read device fam error\n");

	/* Check your board and dts for match the right pmic */
	if (device_fam == 0x09 && (device_id & 0x1F) != 0x0)
		return dev_err_probe(&i2c->dev, -EINVAL, "Device id(%x) mismatched\n",
				     device_id >> 4);

	for (i = 0; i < drvdata->rcnt; i++) {
		const struct regulator_desc *desc;
		const struct pf0900_regulator_desc *r;

		r = &regulator_desc[i];
		desc = &r->desc;
		config.regmap = pf0900->regmap;
		config.driver_data = (void *)r;
		config.dev = pf0900->dev;

		pf0900->rdevs[i] = devm_regulator_register(pf0900->dev, desc, &config);
		if (IS_ERR(pf0900->rdevs[i]))
			return dev_err_probe(pf0900->dev, PTR_ERR(pf0900->rdevs[i]),
					     "Failed to register regulator(%s)\n", desc->name);
	}

	ret = devm_request_threaded_irq(pf0900->dev, pf0900->irq, NULL,
					pf0900_irq_handler,
					(IRQF_TRIGGER_FALLING | IRQF_ONESHOT),
					"pf0900-irq", pf0900);

	if (ret != 0)
		return dev_err_probe(pf0900->dev, ret, "Failed to request IRQ: %d\n",
				     pf0900->irq);
	/*
	 * The PWRUP_M is unmasked by default. When the device enter in RUN state,
	 * it will assert the PWRUP_I interrupt and assert the INTB pin to inform
	 * the MCU that it has finished the power up sequence properly.
	 */
	ret = regmap_write_bits(pf0900->regmap, PF0900_REG_STATUS1_INT, PF0900_IRQ_PWRUP,
				PF0900_IRQ_PWRUP);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Clean PWRUP_I error\n");

	/* mask interrupt PWRUP */
	ret = regmap_update_bits(pf0900->regmap, PF0900_REG_STATUS1_MSK, PF0900_IRQ_PWRUP,
				 PF0900_IRQ_PWRUP);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Unmask irq error\n");

	ret = regmap_update_bits(pf0900->regmap, PF0900_REG_SW_ILIM_MSK, PF0900_IRQ_SW1_IL |
				 PF0900_IRQ_SW2_IL | PF0900_IRQ_SW3_IL | PF0900_IRQ_SW4_IL |
				 PF0900_IRQ_SW5_IL, 0);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Unmask irq error\n");

	ret = regmap_update_bits(pf0900->regmap, PF0900_REG_SW_UV_MSK, PF0900_IRQ_SW1_UV |
				 PF0900_IRQ_SW2_UV | PF0900_IRQ_SW3_UV | PF0900_IRQ_SW4_UV |
				 PF0900_IRQ_SW5_UV, 0);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Unmask irq error\n");

	ret = regmap_update_bits(pf0900->regmap, PF0900_REG_SW_OV_MSK, PF0900_IRQ_SW1_OV |
				 PF0900_IRQ_SW2_OV | PF0900_IRQ_SW3_OV | PF0900_IRQ_SW4_OV |
				 PF0900_IRQ_SW5_OV, 0);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Unmask irq error\n");

	ret = regmap_update_bits(pf0900->regmap, PF0900_REG_LDO_ILIM_MSK, PF0900_IRQ_LDO1_IL |
				 PF0900_IRQ_LDO2_IL | PF0900_IRQ_LDO3_IL, 0);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Unmask irq error\n");

	ret = regmap_update_bits(pf0900->regmap, PF0900_REG_LDO_UV_MSK, PF0900_IRQ_LDO1_UV |
				 PF0900_IRQ_LDO2_UV | PF0900_IRQ_LDO3_UV | PF0900_IRQ_VAON_UV, 0);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Unmask irq error\n");

	ret = regmap_update_bits(pf0900->regmap, PF0900_REG_LDO_OV_MSK, PF0900_IRQ_LDO1_OV |
				 PF0900_IRQ_LDO2_OV | PF0900_IRQ_LDO3_OV | PF0900_IRQ_VAON_OV, 0);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Unmask irq error\n");

	return 0;
}

static struct pf0900_drvdata pf0900_drvdata = {
	.desc = pf0900_regulators,
	.rcnt = ARRAY_SIZE(pf0900_regulators),
};

static const struct of_device_id pf0900_of_match[] = {
	{ .compatible = "nxp,pf0900", .data = &pf0900_drvdata},
	{ }
};

MODULE_DEVICE_TABLE(of, pf0900_of_match);

static struct i2c_driver pf0900_i2c_driver = {
	.driver = {
		.name = "nxp-pf0900",
		.of_match_table = pf0900_of_match,
	},
	.probe = pf0900_i2c_probe,
};

module_i2c_driver(pf0900_i2c_driver);

MODULE_AUTHOR("Joy Zou <joy.zou@nxp.com>");
MODULE_DESCRIPTION("NXP PF0900 Power Management IC driver");
MODULE_LICENSE("GPL");
