// SPDX-License-Identifier: GPL-2.0
/*
 * Chrager driver for cw221x
 *
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/sizes.h>
#include <linux/time.h>
#include <linux/workqueue.h>

/* Module parameters. */
static int debug;
module_param_named(debug, debug, int, 0644);
MODULE_PARM_DESC(debug, "Set to one to enable debugging messages.");

#define cw_printk(fmt, arg...)	\
	{	\
		if (debug)	\
			pr_info("FG_CW221X: %s-%d:" fmt, __func__, __LINE__, ##arg);	\
	}

#define CW_PROPERTIES "cw221X-bat"

#define REG_CHIP_ID		0x00
#define REG_VCELL_H		0x02
#define REG_VCELL_L		0x03
#define REG_SOC_INT		0x04
#define REG_SOC_DECIMAL		0x05
#define REG_TEMP		0x06
#define REG_MODE_CONFIG		0x08
#define REG_GPIO_CONFIG		0x0A
#define REG_SOC_ALERT		0x0B
#define REG_TEMP_MAX		0x0C
#define REG_TEMP_MIN		0x0D
#define REG_CURRENT_H		0x0E
#define REG_CURRENT_L		0x0F
#define REG_T_HOST_H		0xA0
#define REG_T_HOST_L		0xA1
#define REG_USER_CONF		0xA2
#define REG_CYCLE_H		0xA4
#define REG_CYCLE_L		0xA5
#define REG_SOH			0xA6
#define REG_IC_STATE		0xA7
#define REG_FW_VERSION		0xAB
#define REG_BAT_PROFILE		0x10

#define CONFIG_MODE_RESTART	0x30
#define CONFIG_MODE_ACTIVE	0x00
#define CONFIG_MODE_SLEEP	0xF0
#define CONFIG_UPDATE_FLG	0x80
#define IC_VCHIP_ID		0xA0
#define IC_READY_MARK		0x0C

#define GPIO_ENABLE_MIN_TEMP	0
#define GPIO_ENABLE_MAX_TEMP	0
#define GPIO_ENABLE_SOC_CHANGE		0
#define GPIO_SOC_IRQ_VALUE		0x0 /* 0x7F */
#define DEFINED_MAX_TEMP		45
#define DEFINED_MIN_TEMP		0

#define CWFG_NAME			"cw221X"
#define SIZE_OF_PROFILE			80

/* mhom rsense * 1000 for convenience calculation */
#define USER_RSENSE			1500

#define queue_delayed_work_time		5000
#define queue_start_work_time		50

#define CW_SLEEP_20MS			20
#define CW_SLEEP_10MS			10
#define CW_UI_FULL			100
#define COMPLEMENT_CODE_U16		0x8000
#define CW_SLEEP_100MS			100
#define CW_SLEEP_200MS			200
#define CW_SLEEP_COUNTS			50
#define CW_TRUE				1
#define CW_RETRY_COUNT			3
#define CW_VOL_UNIT			1000
#define CW_LOW_VOLTAGE_REF		2500
#define CW_LOW_VOLTAGE			3000
#define CW_LOW_VOLTAGE_STEP		10

#define CW221X_NOT_ACTIVE		1
#define CW221X_PROFILE_NOT_READY	2
#define CW221X_PROFILE_NEED_UPDATE	3

#define CW2215_MARK			0x80
#define CW2217_MARK			0x40
#define CW2218_MARK			0x00

static unsigned char config_profile_info[SIZE_OF_PROFILE] = {
	0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0xB2,
	0xC2, 0xCA, 0xC2, 0xBD, 0x9C, 0x5C, 0x38, 0xFF, 0xFF, 0xC4,
	0x86, 0x74, 0x60, 0x55, 0x4F, 0x4D, 0x4B, 0x80, 0xC0, 0xDB,
	0xCD, 0xD0, 0xCE, 0xD2, 0xD3, 0xD2, 0xD0, 0xCE, 0xC3, 0xD5,
	0xB9, 0xC9, 0xC5, 0xA3, 0x92, 0x8A, 0x80, 0x72, 0x63, 0x62,
	0x74, 0x90, 0xA6, 0x7E, 0x5F, 0x48, 0x80, 0x00, 0xAB, 0x10,
	0x00, 0xA1, 0xFB, 0x00, 0x00, 0x00, 0x64, 0x1E, 0xB1, 0x04,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4D,
};

struct cw_battery {
	struct i2c_client *client;
	struct device *dev;
	struct workqueue_struct *cwfg_workqueue;
	struct delayed_work battery_delay_work;

	struct power_supply *cw_bat;
	u8 *bat_profile;
	int chip_id;
	int voltage;
	int ic_soc_h;
	int ic_soc_l;
	int ui_soc;
	int temp;
	long cw_current;
	int cycle;
	int soh;
	int fw_version;
};

/* CW221X iic read function */
static int cw_read(struct i2c_client *client,
		   unsigned char reg,
		   unsigned char buf[])
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, 1, buf);
	if (ret < 0)
		dev_err(&client->dev, "IIC error %d\n", ret);

	return ret;
}

/* CW221X iic write function */
static int cw_write(struct i2c_client *client,
		    unsigned char reg,
		    unsigned char const buf[])
{
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, reg, 1, &buf[0]);
	if (ret < 0)
		dev_err(&client->dev, "IIC error %d\n", ret);

	return ret;
}

/* CW221X iic read word function */
static int cw_read_word(struct i2c_client *client,
			unsigned char reg,
			unsigned char buf[])
{
	unsigned char reg_val[2] = {0, 0};
	unsigned int temp_val_buff;
	unsigned int temp_val_second;
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, 2, reg_val);
	if (ret < 0)
		dev_err(&client->dev, "IIC error %d\n", ret);
	temp_val_buff = (reg_val[0] << 8) + reg_val[1];

	msleep(CW_SLEEP_10MS);
	ret = i2c_smbus_read_i2c_block_data(client, reg, 2, reg_val);
	if (ret < 0)
		dev_err(&client->dev, "IIC error %d\n", ret);
	temp_val_second = (reg_val[0] << 8) + reg_val[1];

	if (temp_val_buff != temp_val_second) {
		msleep(CW_SLEEP_10MS);
		ret = i2c_smbus_read_i2c_block_data(client, reg, 2, reg_val);
		if (ret < 0) {
			dev_err(&client->dev, "IIC error %d\n", ret);
			return ret;
		}
	}

	buf[0] = reg_val[0];
	buf[1] = reg_val[1];

	return ret;
}

/* CW221X iic write profile function */
static int cw_write_profile(struct i2c_client *client, unsigned char const buf[])
{
	int ret;
	int i;

	for (i = 0; i < SIZE_OF_PROFILE; i++) {
		ret = cw_write(client, REG_BAT_PROFILE + i, &buf[i]);
		if (ret < 0) {
			dev_err(&client->dev, "IIC error %d\n", ret);
			return ret;
		}
	}

	return ret;
}

/*
 * CW221X Active function
 * The CONFIG register is used for the host MCU to configure the fuel gauge IC.
 * The default value is 0xF0, SLEEP and RESTART bits are set. To power up the IC,
 * the host MCU needs to write 0x30 to exit shutdown mode, and then write 0x00 to
 * restart the gauge to enter active mode. To reset the IC, the host MCU needs
 * to write 0xF0, 0x30 and 0x00 in sequence to this register to complete the
 * restart procedure. The CW221X will reload relevant parameters and settings and
 * restart SOC calculation. Note that the SOC may be a different value after reset
 * operation since it is a brand-new calculation based on the latest battery status.
 * CONFIG [3:0] is reserved. Don't do any operation with it.
 */
static int cw221X_active(struct cw_battery *cw_bat)
{
	unsigned char reg_val = CONFIG_MODE_RESTART;
	int ret;

	ret = cw_write(cw_bat->client, REG_MODE_CONFIG, &reg_val);
	if (ret < 0)
		return ret;
	msleep(CW_SLEEP_20MS); /* Here delay must >= 20 ms */

	reg_val = CONFIG_MODE_ACTIVE;
	ret = cw_write(cw_bat->client, REG_MODE_CONFIG, &reg_val);
	if (ret < 0)
		return ret;
	msleep(CW_SLEEP_10MS);

	return 0;
}

/*
 * CW221X Sleep function
 * The CONFIG register is used for the host MCU to configure the fuel gauge IC.
 * The default value is 0xF0,SLEEP and RESTART bits are set. To power up the IC,
 * the host MCU needs to write 0x30 to exit shutdown mode, and then write 0x00
 * to restart the gauge to enter active mode. To reset the IC, the host MCU needs
 * to write 0xF0, 0x30 and 0x00 in sequence to this register to complete the restart
 * procedure. The CW221X will reload relevant parameters and settings and restart SOC
 * calculation. Note that the SOC may be a different value after reset operation since
 * it is a brand-new calculation based on the latest battery status.
 * CONFIG [3:0] is reserved. Don't do any operation with it.
 */
static int cw221X_sleep(struct cw_battery *cw_bat)
{
	unsigned char reg_val = CONFIG_MODE_RESTART;
	int ret;

	ret = cw_write(cw_bat->client, REG_MODE_CONFIG, &reg_val);
	if (ret < 0)
		return ret;
	msleep(CW_SLEEP_20MS); /* Here delay must >= 20 ms */

	reg_val = CONFIG_MODE_SLEEP;
	ret = cw_write(cw_bat->client, REG_MODE_CONFIG, &reg_val);
	if (ret < 0)
		return ret;
	msleep(CW_SLEEP_10MS);

	return 0;
}

/*
 * The 0x00 register is an UNSIGNED 8bit read-only register. Its value is
 * fixed to 0xA0 in shutdown mode and active mode.
 */
static int cw_get_chip_id(struct cw_battery *cw_bat)
{
	unsigned char reg_val;
	int chip_id;
	int ret;

	ret = cw_read(cw_bat->client, REG_CHIP_ID, &reg_val);
	if (ret < 0)
		return ret;

	chip_id = reg_val; /* This value must be 0xA0! */
	pr_info("CW: chip_id = 0x%x\n", chip_id);
	cw_bat->chip_id = chip_id;

	return 0;
}

/*
 * The VCELL register(0x02 0x03) is an UNSIGNED 14bit read-only register that
 * updates the battery voltage continuously. Battery voltage is measured between
 * the VCELL pin and VSS pin, which is the ground reference. A 14bit
 * sigma-delta A/D converter is used and the voltage resolution is 312.5uV. (0.3125mV is *5/16)
 */
static int cw_get_voltage(struct cw_battery *cw_bat)
{
	unsigned char reg_val[2] = {0, 0};
	unsigned int voltage;
	int ret;

	ret = cw_read_word(cw_bat->client, REG_VCELL_H, reg_val);
	if (ret < 0)
		return ret;

	voltage = (reg_val[0] << 8) + reg_val[1];
	voltage = voltage * 5 / 16;
	cw_bat->voltage = voltage;

	return 0;
}

/*
 * The SOC register(0x04 0x05) is an UNSIGNED 16bit read-only register that indicates
 * the SOC of the battery. The SOC shows in % format, which means how much percent of
 * the battery's total available capacity is remaining in the battery now. The SOC can
 * intrinsically adjust itself to cater to the change of battery status,
 * including load, temperature and aging etc.
 * The high byte(0x04) contains the SOC in 1% unit which can be directly used if
 * this resolution is good enough for the application. The low byte(0x05) provides
 * more accurate fractional part of the SOC and its
 * LSB is (1/256) %.
 */
static int cw_get_capacity(struct cw_battery *cw_bat)
{
	unsigned char reg_val[2] = {0, 0};
	int ui_100 = CW_UI_FULL;
	int ui_soc;
	int soc_h;
	int soc_l;
	int ret;

	ret = cw_read_word(cw_bat->client, REG_SOC_INT, reg_val);
	if (ret < 0)
		return ret;
	soc_h = reg_val[0];
	soc_l = reg_val[1];
	ui_soc = ((soc_h * 256 + soc_l) * 100) / (ui_100 * 256);
	/* remainder = (((soc_h * 256 + soc_l) * 100 * 100) / (ui_100 * 256)) % 100; */
	if (ui_soc >= 100) {
		cw_printk("CW221x[%d]: UI_SOC = %d larger 100!\n", __LINE__, ui_soc);
		ui_soc = 100;
	}
	cw_bat->ic_soc_h = soc_h;
	cw_bat->ic_soc_l = soc_l;
	cw_bat->ui_soc = ui_soc;

	return 0;
}

/*
 * The TEMP register is an UNSIGNED 8bit read only register.
 * It reports the real-time battery temperature
 * measured at TS pin. The scope is from -40 to 87.5 degrees Celsius,
 * LSB is 0.5 degree Celsius. TEMP(C) = - 40 + Value(0x06 Reg) / 2
 */
static int cw_get_temp(struct cw_battery *cw_bat)
{
	unsigned char reg_val;
	int temp;
	int ret;

	ret = cw_read(cw_bat->client, REG_TEMP, &reg_val);
	if (ret < 0)
		return ret;

	temp = (int)reg_val * 10 / 2 - 400;
	cw_bat->temp = temp;

	return 0;
}

/* get complement code function, unsigned short must be U16 */
static long get_complement_code(unsigned short raw_code)
{
	long complement_code;
	int dir;

	if (0 != (raw_code & COMPLEMENT_CODE_U16)) {
		dir = -1;
		raw_code =  (0xFFFF - raw_code) + 1;
	} else {
		dir = 1;
	}
	complement_code = (long)raw_code * dir;

	return complement_code;
}

/*
 * CURRENT is a SIGNED 16bit register(0x0E 0x0F) that reports current A/D converter
 * result of the voltage across the current sense resistor, 10mohm typical.
 * The result is stored as a two's complement value to show positive and negative current.
 * Voltages outside the minimum and maximum register values are reported as the
 * minimum or maximum value. The register value should be divided by the sense
 * resistance to convert to amperes. The value of the sense resistor determines
 * the resolution and the full-scale range of the current readings. The LSB of 0x0F
 * is (52.4/32768)uV for CW2215 and CW2217. The LSB of 0x0F is (125/32768)uV for CW2218.
 * The default value is 0x0000, stands for 0mA. 0x7FFF stands for the maximum charging
 * current and 0x8001 stands for the maximum discharging current.
 */
static int cw_get_current(struct cw_battery *cw_bat)
{
	unsigned char reg_val[2] = {0, 0};
	unsigned short current_reg; /* unsigned short must u16 */
	long long cw_current; /* use long long type to guarantee 8 bytes space */
	int ret;

	ret = cw_read_word(cw_bat->client, REG_CURRENT_H, reg_val);
	if (ret < 0)
		return ret;

	current_reg = (reg_val[0] << 8) + reg_val[1];
	cw_current = get_complement_code(current_reg);

	if (((cw_bat->fw_version & CW2215_MARK) != 0) || ((cw_bat->fw_version & CW2217_MARK) != 0))
		cw_current = cw_current * 1600 / USER_RSENSE;
	else if ((cw_bat->fw_version != 0) && ((cw_bat->fw_version & 0xC0) == CW2218_MARK))
		cw_current = cw_current * 3815 / USER_RSENSE;
	else {
		cw_bat->cw_current = 0;
		dev_err(cw_bat->dev, "error! cw221x firmware read error!\n");
	}

	cw_bat->cw_current = cw_current;

	return 0;
}

/*
 * CYCLECNT is an UNSIGNED 16bit register(0xA4 0xA5) that counts cycle life of the battery.
 * The LSB of 0xA5 stands for 1/16 cycle. This register will be clear after enters shutdown mode
 */
static int cw_get_cycle_count(struct cw_battery *cw_bat)
{
	unsigned char reg_val[2] = {0, 0};
	int cycle;
	int ret;

	ret = cw_read_word(cw_bat->client, REG_CYCLE_H, reg_val);
	if (ret < 0)
		return ret;

	cycle = (reg_val[0] << 8) + reg_val[1];
	cw_bat->cycle = cycle / 16;

	return 0;
}

/*
 * SOH (State of Health) is an UNSIGNED 8bit register(0xA6) that represents the level of
 * battery aging by tracking battery internal impedance increment. When the device enters
 * active mode, this register refresh to 0x64 by default. Its range is 0x00 to 0x64,
 * indicating 0 to 100%. This register will be clear after enters shutdown mode.
 */
static int cw_get_soh(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reg_val;
	int soh;

	ret = cw_read(cw_bat->client, REG_SOH, &reg_val);
	if (ret < 0)
		return ret;

	soh = reg_val;
	cw_bat->soh = soh;

	return 0;
}

/*
 * FW_VERSION register reports the firmware (FW) running in the chip. It is fixed to
 * 0x00 when the chip is in shutdown mode. When in active mode, Bit [7:6] = '01' stand
 * for the CW2217, Bit [7:6] = '00' stand for the CW2218 and Bit [7:6] = '10' stand for CW2215.
 * Bit[5:0] stand for the FW version running in the chip. Note that the FW version is
 * subject to update and contact sales office for confirmation when necessary.
 */
static int cw_get_fw_version(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reg_val;
	int fw_version;

	ret = cw_read(cw_bat->client, REG_FW_VERSION, &reg_val);
	if (ret < 0)
		return ret;

	fw_version = reg_val;
	cw_bat->fw_version = fw_version;

	return 0;
}

static int cw_update_data(struct cw_battery *cw_bat)
{
	int ret = 0;

	ret += cw_get_voltage(cw_bat);
	ret += cw_get_capacity(cw_bat);
	ret += cw_get_temp(cw_bat);
	ret += cw_get_current(cw_bat);
	ret += cw_get_cycle_count(cw_bat);
	ret += cw_get_soh(cw_bat);
	cw_printk("vol = %d  current = %ld cap = %d temp = %d\n",
		  cw_bat->voltage, cw_bat->cw_current, cw_bat->ui_soc, cw_bat->temp);

	return ret;
}

static int cw_init_data(struct cw_battery *cw_bat)
{
	int ret = 0;

	ret = cw_get_fw_version(cw_bat);
	if (ret != 0)
		return ret;

	ret += cw_get_chip_id(cw_bat);
	ret += cw_get_voltage(cw_bat);
	ret += cw_get_capacity(cw_bat);
	ret += cw_get_temp(cw_bat);
	ret += cw_get_current(cw_bat);
	ret += cw_get_cycle_count(cw_bat);
	ret += cw_get_soh(cw_bat);

	cw_printk("chip_id = %d vol = %d  cur = %ld cap = %d temp = %d  fw_version = %d\n",
		  cw_bat->chip_id, cw_bat->voltage, cw_bat->cw_current,
		  cw_bat->ui_soc, cw_bat->temp, cw_bat->fw_version);

	return ret;
}

static int cw221x_parse_properties(struct cw_battery *cw_bat)
{
	struct device *dev = cw_bat->dev;
	int length;
	int ret;

	length = device_property_count_u8(dev, "cellwise,battery-profile");
	if (length < 0) {
		dev_warn(cw_bat->dev,
			 "No battery-profile found, using current flash contents\n");
	} else if (length != SIZE_OF_PROFILE) {
		dev_err(cw_bat->dev, "battery-profile must be %d bytes\n",
			SIZE_OF_PROFILE);
		return -EINVAL;
	}

	cw_bat->bat_profile = devm_kzalloc(dev, length, GFP_KERNEL);
	if (!cw_bat->bat_profile)
		return -ENOMEM;

	ret = device_property_read_u8_array(dev,
					    "cellwise,battery-profile",
					    cw_bat->bat_profile,
					    length);

	return ret;
}

static void cw_config_profile_init(struct cw_battery *cw_bat)
{
	int i, ret;

	ret = cw221x_parse_properties(cw_bat);
	if (ret) {
		/* update new battery info */
		cw_bat->bat_profile = config_profile_info;
		cw_printk("the driver profile:\n");
	}

	for (i = 0; i < SIZE_OF_PROFILE; i++)
		cw_printk("[%d]: 0x%x\n", i, cw_bat->bat_profile[i]);
}

/*CW221X update profile function, Often called during initialization*/
static int cw_config_start_ic(struct cw_battery *cw_bat)
{
	unsigned char reg_val;
	int count = 0;
	int i, ret;

	ret = cw221X_sleep(cw_bat);
	if (ret < 0)
		return ret;

	/* update new battery info */
	ret = cw_write_profile(cw_bat->client, cw_bat->bat_profile);
	if (ret < 0)
		return ret;

	cw_printk("the driver profile:\n");
	for (i = 0; i < SIZE_OF_PROFILE; i++)
		cw_printk("[%d]: 0x%x\n", i, cw_bat->bat_profile[i]);

	/* set UPDATE_FLAG AND SOC INTTERRUP VALUE */
	reg_val = CONFIG_UPDATE_FLG | GPIO_SOC_IRQ_VALUE;
	ret = cw_write(cw_bat->client, REG_SOC_ALERT, &reg_val);
	if (ret < 0)
		return ret;

	/* close all interruptes */
	reg_val = 0;
	ret = cw_write(cw_bat->client, REG_GPIO_CONFIG, &reg_val);
	if (ret < 0)
		return ret;

	ret = cw221X_active(cw_bat);
	if (ret < 0)
		return ret;

	while (CW_TRUE) {
		msleep(CW_SLEEP_100MS);
		cw_read(cw_bat->client, REG_IC_STATE, &reg_val);
		if (IC_READY_MARK == (reg_val & IC_READY_MARK))
			break;
		count++;
		if (count >= CW_SLEEP_COUNTS) {
			cw221X_sleep(cw_bat);
			return -1;
		}
	}

	return 0;
}

/*
 * Get the cw221X running state
 * Determine whether the profile needs to be updated
 */
static int cw221X_get_state(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reg_val;
	int i;
	int reg_profile;

	ret = cw_read(cw_bat->client, REG_MODE_CONFIG, &reg_val);
	if (ret < 0)
		return ret;
	if (reg_val != CONFIG_MODE_ACTIVE)
		return CW221X_NOT_ACTIVE;

	ret = cw_read(cw_bat->client, REG_SOC_ALERT, &reg_val);
	if (ret < 0)
		return ret;
	if (0x00 == (reg_val & CONFIG_UPDATE_FLG))
		return CW221X_PROFILE_NOT_READY;

	for (i = 0; i < SIZE_OF_PROFILE; i++) {
		ret = cw_read(cw_bat->client, (REG_BAT_PROFILE + i), &reg_val);
		if (ret < 0)
			return ret;
		reg_profile = REG_BAT_PROFILE + i;
		cw_printk("fuelgauge: 0x%2x = 0x%2x\n", reg_profile, reg_val);
		if (cw_bat->bat_profile[i] != reg_val)
			break;
	}
	if (i != SIZE_OF_PROFILE)
		return CW221X_PROFILE_NEED_UPDATE;

	return 0;
}

/* CW221X init function, Often called during initialization */
static int cw_init(struct cw_battery *cw_bat)
{
	int ret;

	ret = cw_get_chip_id(cw_bat);
	if (ret < 0) {
		dev_err(cw_bat->dev, "iic read write error");
		return ret;
	}
	if (cw_bat->chip_id != IC_VCHIP_ID) {
		dev_err(cw_bat->dev, "not cw221X\n");
		return -1;
	}

	ret = cw221X_get_state(cw_bat);
	if (ret < 0) {
		dev_err(cw_bat->dev, "iic read write error");
		return ret;
	}

	if (ret != 0) {
		cw_printk("need update profile\n");

		ret = cw_config_start_ic(cw_bat);
		if (ret < 0)
			return ret;
	} else
		cw_printk("not need update profile\n");
	cw_printk("cw221X init success!\n");

	return 0;
}

static void cw_bat_work(struct work_struct *work)
{
	struct delayed_work *delay_work;
	struct cw_battery *cw_bat;
	static int soc;
	int ret;

	delay_work = container_of(work,
				  struct delayed_work,
				  work);
	cw_bat = container_of(delay_work,
			      struct cw_battery,
			      battery_delay_work);

	ret = cw_update_data(cw_bat);
	if (ret < 0)
		dev_err(cw_bat->dev, "i2c read error when update data");
	if (cw_bat->ui_soc != soc) {
		soc = cw_bat->ui_soc;
		power_supply_changed(cw_bat->cw_bat);
	}
	queue_delayed_work(cw_bat->cwfg_workqueue,
			   &cw_bat->battery_delay_work,
			   msecs_to_jiffies(queue_delayed_work_time));
}

static int cw_battery_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	/* struct cw_battery *cw_bat = power_supply_get_drvdata(psy); */
	int ret = 0;

	switch (psp) {
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int cw_battery_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct cw_battery *cw_bat = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = cw_bat->cycle;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = cw_bat->ui_soc;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if ((cw_bat->ui_soc < 1) && (!power_supply_is_system_supplied()))
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else if (cw_bat->ui_soc <= 20)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (cw_bat->ui_soc <= 70)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		else if (cw_bat->ui_soc <= 90)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
		else
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (cw_bat->ui_soc == 100 * 1000)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else {
			if (power_supply_is_system_supplied())
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = 10 * 1000 * 1000;/* uAh */
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = (cw_bat->voltage <= 0) ? 0 : 1;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		cw_get_voltage(cw_bat);
		val->intval = cw_bat->voltage * CW_VOL_UNIT;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		cw_get_current(cw_bat);
		val->intval = cw_bat->cw_current * 1000; /* uA */
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = cw_bat->temp;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property cw_battery_properties[] = {
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TEMP,
};

static int cw221X_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct power_supply_config psy_cfg = {0};
	struct power_supply_desc *psy_desc;
	struct cw_battery *cw_bat;
	int loop = 0;
	int ret;

	cw_bat = devm_kzalloc(&client->dev, sizeof(*cw_bat), GFP_KERNEL);
	if (!cw_bat)
		return -ENOMEM;

	i2c_set_clientdata(client, cw_bat);
	cw_bat->client = client;
	cw_bat->dev = &client->dev;

	dev_dbg(cw_bat->dev, "cw221X driver versions-%d\n", 20220830);
	cw_config_profile_init(cw_bat);
	ret = cw_init(cw_bat);
	while ((loop++ < CW_RETRY_COUNT) && (ret != 0)) {
		msleep(CW_SLEEP_200MS);
		ret = cw_init(cw_bat);
	}
	if (ret) {
		dev_err(cw_bat->dev, "cw221X init fail!\n");
		return ret;
	}

	ret = cw_init_data(cw_bat);
	if (ret) {
		dev_err(cw_bat->dev, "cw221X init data fail!\n");
		return ret;
	}

	psy_desc = devm_kzalloc(&client->dev, sizeof(*psy_desc), GFP_KERNEL);
	if (!psy_desc)
		return -ENOMEM;
	psy_cfg.drv_data = cw_bat;
	psy_desc->name = CW_PROPERTIES;
	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->properties = cw_battery_properties;
	psy_desc->num_properties = ARRAY_SIZE(cw_battery_properties);
	psy_desc->get_property = cw_battery_get_property;
	psy_desc->set_property = cw_battery_set_property;
	cw_bat->cw_bat = devm_power_supply_register(&client->dev, psy_desc, &psy_cfg);
	if (IS_ERR(cw_bat->cw_bat)) {
		ret = PTR_ERR(cw_bat->cw_bat);
		dev_err(cw_bat->dev, "failed to register battery: %d\n", ret);
		return ret;
	}

	cw_bat->cwfg_workqueue = create_singlethread_workqueue("cwfg_gauge");
	INIT_DELAYED_WORK(&cw_bat->battery_delay_work, cw_bat_work);
	queue_delayed_work(cw_bat->cwfg_workqueue,
			   &cw_bat->battery_delay_work,
			   msecs_to_jiffies(queue_start_work_time));

	cw_printk("cw221X driver probe success!\n");

	return 0;
}

static int cw221X_remove(struct i2c_client *client)
{
	struct cw_battery *cw_bat = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&cw_bat->battery_delay_work);
	destroy_workqueue(cw_bat->cwfg_workqueue);
	return 0;
}

#ifdef CONFIG_PM
static int cw_bat_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cw_battery *cw_bat = i2c_get_clientdata(client);

	cancel_delayed_work(&cw_bat->battery_delay_work);
	return 0;
}

static int cw_bat_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cw_battery *cw_bat = i2c_get_clientdata(client);

	queue_delayed_work(cw_bat->cwfg_workqueue,
			   &cw_bat->battery_delay_work,
			   msecs_to_jiffies(20));
	return 0;
}

static const struct dev_pm_ops cw_bat_pm_ops = {
	.suspend = cw_bat_suspend,
	.resume = cw_bat_resume,
};
#endif

static const struct i2c_device_id cw221X_id_table[] = {
	{ CWFG_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cw221X_id_table);

#ifdef CONFIG_OF
static const struct of_device_id cw221X_match_table[] = {
	{ .compatible = "cellwise,cw221X", },
	{ },
};
MODULE_DEVICE_TABLE(of, cw221X_match_table);
#endif

static struct i2c_driver cw221X_driver = {
	.driver = {
		.name = CWFG_NAME,
#ifdef CONFIG_PM
		.pm = &cw_bat_pm_ops,
#endif
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cw221X_match_table),
	},
	.probe = cw221X_probe,
	.remove = cw221X_remove,
	.id_table = cw221X_id_table,
};

module_i2c_driver(cw221X_driver);

MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_DESCRIPTION("CW221X FGADC Device Driver V0.1");
MODULE_LICENSE("GPL");
