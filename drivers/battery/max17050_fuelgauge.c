/*
 *  max17050_fuelgauge.c
 *  Samsung MAX17050 Fuel Gauge Driver
 *
 *  Copyright (C) 2012 Samsung Electronics
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DEBUG

#include <linux/battery/sec_fuelgauge.h>

#ifdef CONFIG_FUELGAUGE_MAX17050_VOLTAGE_TRACKING
static int max17050_write_reg(struct i2c_client *client, int reg, u8 *buf)
{
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, reg, 2, buf);

	if (ret < 0)
		dev_err(&client->dev, "%s: Error(%d)\n", __func__, ret);

	return ret;
}

static int max17050_read_reg(struct i2c_client *client, int reg, u8 *buf)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, 2, buf);

	if (ret < 0)
		dev_err(&client->dev, "%s: Error(%d)\n", __func__, ret);

	return ret;
}

static void max17050_write_reg_array(struct i2c_client *client,
				     u8 *buf, int size)
{
	int i;

	for (i = 0; i < size; i += 3)
		max17050_write_reg(client, (u8) (*(buf + i)), (buf + i) + 1);
}

static void max17050_init_regs(struct i2c_client *client)
{
	u8 data[2];

	if (max17050_read_reg(client, MAX17050_REG_FILTERCFG, data) < 0)
		return;

	/* Clear average vcell (12 sec) */
	data[0] &= 0x8f;

	max17050_write_reg(client, MAX17050_REG_FILTERCFG, data);
}

static void max17050_get_version(struct i2c_client *client)
{
	u8 data[2];

	if (max17050_read_reg(client, MAX17050_REG_VERSION, data) < 0)
		return;

	dev_dbg(&client->dev, "MAX17050 Fuel-Gauge Ver %d%d\n",
		data[0], data[1]);
}

static void max17050_alert_init(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data[2];

	/* SALRT Threshold setting */
	data[0] = fuelgauge->pdata->fuel_alert_soc;
	data[1] = 0xff;
	max17050_write_reg(client, MAX17050_REG_SALRT_TH, data);

	/* VALRT Threshold setting */
	data[0] = 0x00;
	data[1] = 0xff;
	max17050_write_reg(client, MAX17050_REG_VALRT_TH, data);

	/* TALRT Threshold setting */
	data[0] = 0x80;
	data[1] = 0x7f;
	max17050_write_reg(client, MAX17050_REG_TALRT_TH, data);
}

static bool max17050_check_status(struct i2c_client *client)
{
	u8 data[2];
	bool ret = false;

	/* check if Smn was generated */
	if (max17050_read_reg(client, MAX17050_REG_STATUS, data) < 0)
		return ret;

	dev_info(&client->dev, "%s: status_reg(%02x%02x)\n",
		__func__, data[1], data[0]);

	/* minimum SOC threshold exceeded. */
	if (data[1] & (0x1 << 2))
		ret = true;

	/* clear status reg */
	if (!ret) {
		data[1] = 0;
		max17050_write_reg(client, MAX17050_REG_STATUS, data);
		msleep(200);
	}

	return ret;
}

static int max17050_set_temperature(struct i2c_client *client, int temperature)
{
	u8 data[2];

	data[0] = 0;
	data[1] = temperature;
	max17050_write_reg(client, MAX17050_REG_TEMPERATURE, data);

	dev_dbg(&client->dev, "%s: temperature to (%d)\n",
		__func__, temperature);

	return temperature;
}

static int max17050_get_temperature(struct i2c_client *client)
{
	u8 data[2];
	s32 temperature = 0;

	if (max17050_read_reg(client, MAX17050_REG_TEMPERATURE, data) < 0)
		return -ERANGE;

	/* data[] store 2's compliment format number */
	if (data[1] & (0x1 << 7)) {
		/* Negative */
		temperature = ((~(data[1])) & 0xFF) + 1;
		temperature *= (-1000);
	} else {
		temperature = data[1] & 0x7F;
		temperature *= 1000;
		temperature += data[0] * 39 / 10;
	}

	dev_dbg(&client->dev, "%s: temperature (%d)\n",
		__func__, temperature);

	return temperature;
}

/* soc should be 0.1% unit */
static int max17050_get_soc(struct i2c_client *client)
{
	u8 data[2];
	int soc;

	if (max17050_read_reg(client, MAX17050_REG_SOC_VF, data) < 0)
		return -EINVAL;

	soc = ((data[1] * 100) + (data[0] * 100 / 256)) / 10;

	dev_dbg(&client->dev, "%s: raw capacity (%d)\n", __func__, soc);

	return min(soc, 1000);
}

static int max17050_get_vfocv(struct i2c_client *client)
{
	u8 data[2];
	u32 vfocv = 0;

	if (max17050_read_reg(client, MAX17050_REG_VFOCV, data) < 0)
		return -EINVAL;

	vfocv = ((data[0] >> 3) + (data[1] << 5)) * 625 / 1000;

	dev_dbg(&client->dev, "%s: vfocv (%d)\n", __func__, vfocv);

	return vfocv;
}

static int max17050_get_vcell(struct i2c_client *client)
{
	u8 data[2];
	u32 vcell = 0;

	if (max17050_read_reg(client, MAX17050_REG_VCELL, data) < 0)
		return -EINVAL;

	vcell = ((data[0] >> 3) + (data[1] << 5)) * 625 / 1000;

	dev_dbg(&client->dev, "%s: vcell (%d)\n", __func__, vcell);

	return vcell;
}

static int max17050_get_avgvcell(struct i2c_client *client)
{
	u8 data[2];
	u32 avgvcell = 0;

	if (max17050_read_reg(client, MAX17050_REG_AVGVCELL, data) < 0)
		return -EINVAL;

	avgvcell = ((data[0] >> 3) + (data[1] << 5)) * 625 / 1000;

	dev_dbg(&client->dev, "%s: avgvcell (%d)\n", __func__, avgvcell);

	return avgvcell;
}

bool sec_hal_fg_init(struct i2c_client *client)
{
	/* initialize fuel gauge registers */
	max17050_init_regs(client);

	max17050_get_version(client);

	return true;
}

bool sec_hal_fg_suspend(struct i2c_client *client)
{
	return true;
}

bool sec_hal_fg_resume(struct i2c_client *client)
{
	return true;
}

bool sec_hal_fg_fuelalert_init(struct i2c_client *client, int soc)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data[2];

	/* 1. Set max17050 alert configuration. */
	max17050_alert_init(client);

	if (max17050_read_reg(client, MAX17050_REG_CONFIG, data)
	    < 0)
		return -1;

	/*Enable Alert (Aen = 1) */
	data[0] |= (0x1 << 2);

	max17050_write_reg(client, MAX17050_REG_CONFIG, data);

	dev_dbg(&client->dev, "%s: config_reg(%02x%02x) irq(%d)\n",
		 __func__, data[1], data[0], fuelgauge->pdata->fg_gpio_irq);

	return true;
}

bool sec_hal_fg_is_fuelalerted(struct i2c_client *client)
{
	return max17050_check_status(client);
}

bool sec_hal_fg_fuelalert_process(void *irq_data, bool is_fuel_alerted)
{
	struct sec_fuelgauge_info *fuelgauge = irq_data;
	u8 data[2];

	/* update SOC */
	/* max17050_get_soc(fuelgauge->client); */

	if (is_fuel_alerted) {
		if (max17050_read_reg(fuelgauge->client,
				      MAX17050_REG_CONFIG, data) < 0)
			return false;

		data[1] |= (0x1 << 3);

		max17050_write_reg(fuelgauge->client,
				   MAX17050_REG_CONFIG, data);

		dev_info(&fuelgauge->client->dev,
			"%s: Fuel-alert Alerted!! (%02x%02x)\n",
			__func__, data[1], data[0]);
	} else {
		if (max17050_read_reg(fuelgauge->client,
				      MAX17050_REG_CONFIG, data)
		    < 0)
			return false;

		data[1] &= (~(0x1 << 3));

		max17050_write_reg(fuelgauge->client,
				   MAX17050_REG_CONFIG, data);

		dev_info(&fuelgauge->client->dev,
			"%s: Fuel-alert Released!! (%02x%02x)\n",
			__func__, data[1], data[0]);
	}

	max17050_read_reg(fuelgauge->client, MAX17050_REG_VCELL, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_VCELL(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_TEMPERATURE, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_TEMPERATURE(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_CONFIG, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_CONFIG(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_VFOCV, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_VFOCV(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_SOC_VF, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_SOC_VF(%02x%02x)\n",
		 __func__, data[1], data[0]);

	dev_dbg(&fuelgauge->client->dev,
		"%s: FUEL GAUGE IRQ (%d)\n",
		 __func__,
		 gpio_get_value(irq_to_gpio(fuelgauge->pdata->fg_gpio_irq)));

#if 0
	max17050_read_reg(fuelgauge->client, MAX17050_REG_STATUS, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_STATUS(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_VALRT_TH, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_VALRT_TH(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_TALRT_TH, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_TALRT_TH(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_SALRT_TH, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_SALRT_TH(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_AVGVCELL, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_AVGVCELL(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_VERSION, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_VERSION(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_LEARNCFG, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_LEARNCFG(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_MISCCFG, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_MISCCFG(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_CGAIN, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_CGAIN(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17050_read_reg(fuelgauge->client, MAX17050_REG_RCOMP, data);
	dev_dbg(&fuelgauge->client->dev,
		"%s: MAX17050_REG_RCOMP(%02x%02x)\n",
		 __func__, data[1], data[0]);
#endif

	return true;
}

bool sec_hal_fg_full_charged(struct i2c_client *client)
{
	return true;
}

bool sec_hal_fg_get_property(struct i2c_client *client,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	switch (psp) {
		/* Cell voltage (VCELL, mV) */
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = max17050_get_vcell(client);
		break;
		/* Additional Voltage Information (mV) */
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		switch (val->intval) {
		case SEC_BATTEY_VOLTAGE_AVERAGE:
			val->intval = max17050_get_avgvcell(client);
			break;
		case SEC_BATTEY_VOLTAGE_OCV:
			val->intval = max17050_get_vfocv(client);
			break;
		}
		break;
		/* Current (mA) */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = 0;
		break;
		/* Average Current (mA) */
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = 0;
		break;
		/* SOC (%) */
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = max17050_get_soc(client);
		break;
		/* Battery Temperature */
	case POWER_SUPPLY_PROP_TEMP:
		/* Target Temperature */
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		val->intval = max17050_get_temperature(client);
		break;
	default:
		return false;
	}
	return true;
}

bool sec_hal_fg_set_property(struct i2c_client *client,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	switch (psp) {
		/* Battery Temperature */
	case POWER_SUPPLY_PROP_TEMP:
		/* Target Temperature */
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		max17050_set_temperature(client, val->intval);
		break;
	default:
		return false;
	}
	return true;
}

ssize_t sec_hal_fg_show_attrs(struct device *dev,
				const ptrdiff_t offset, char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sec_fuelgauge_info *fg =
		container_of(psy, struct sec_fuelgauge_info, psy_fg);
	int i = 0;

	switch (offset) {
/*	case FG_REG: */
/*		break; */
	case FG_DATA:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%02x%02x\n",
			fg->reg_data[1], fg->reg_data[0]);
		break;
	default:
		i = -EINVAL;
		break;
	}

	return i;
}

ssize_t sec_hal_fg_store_attrs(struct device *dev,
				const ptrdiff_t offset,
				const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sec_fuelgauge_info *fg =
		container_of(psy, struct sec_fuelgauge_info, psy_fg);
	int ret = 0;
	int x = 0;
	u8 data[2];

	switch (offset) {
	case FG_REG:
		if (sscanf(buf, "%x\n", &x) == 1) {
			fg->reg_addr = x;
			max17050_read_reg(fg->client,
				fg->reg_addr, fg->reg_data);
			dev_dbg(&fg->client->dev,
				"%s: (read) addr = 0x%x, data = 0x%02x%02x\n",
				 __func__, fg->reg_addr,
				 fg->reg_data[1], fg->reg_data[0]);
			ret = count;
		}
		break;
	case FG_DATA:
		if (sscanf(buf, "%x\n", &x) == 1) {
			data[0] = (x & 0xFF00) >> 8;
			data[1] = (x & 0x00FF);
			dev_dbg(&fg->client->dev,
				"%s: (write) addr = 0x%x, data = 0x%02x%02x\n",
				__func__, fg->reg_addr, data[1], data[0]);
			max17050_write_reg(fg->client,
				fg->reg_addr, data);
			ret = count;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
#endif

#ifdef CONFIG_FUELGAUGE_MAX17050_COULOMB_COUNTING
static int fg_i2c_read(struct i2c_client *client,
				u8 reg, u8 *data, u8 length)
{
	s32 value;

	value = i2c_smbus_read_i2c_block_data(client, reg, length, data);
	if (value < 0 || value != length) {
		dev_err(&client->dev, "%s: Error(%d)\n",
			__func__, value);
		return -1;
	}

	return 0;
}

static int fg_i2c_write(struct i2c_client *client,
				u8 reg, u8 *data, u8 length)
{
	s32 value;

	value = i2c_smbus_write_i2c_block_data(client, reg, length, data);
	if (value < 0) {
		dev_err(&client->dev, "%s: Error(%d)\n",
			__func__, value);
		return -1;
	}

	return 0;
}

static int fg_read_register(struct i2c_client *client,
				u8 addr)
{
	u8 data[2];

	if (fg_i2c_read(client, addr, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read addr(0x%x)\n",
			__func__, addr);
		return -1;
	}

	return (data[1] << 8) | data[0];
}

static int fg_write_register(struct i2c_client *client,
				u8 addr, u16 w_data)
{
	u8 data[2];

	data[0] = w_data & 0xFF;
	data[1] = w_data >> 8;

	if (fg_i2c_write(client, addr, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to write addr(0x%x)\n",
			__func__, addr);
		return -1;
	}

	return 0;
}

static int fg_read_16register(struct i2c_client *client,
				u8 addr, u16 *r_data)
{
	u8 data[32];
	int i = 0;

	if (fg_i2c_read(client, addr, data, 32) < 0) {
		dev_err(&client->dev, "%s: Failed to read addr(0x%x)\n",
			__func__, addr);
		return -1;
	}

	for (i = 0; i < 16; i++)
		r_data[i] = (data[2 * i + 1] << 8) | data[2 * i];

	return 0;
}

static void fg_write_and_verify_register(struct i2c_client *client,
				u8 addr, u16 w_data)
{
	u16 r_data;
	u8 retry_cnt = 2;

	while (retry_cnt) {
		fg_write_register(client, addr, w_data);
		r_data = fg_read_register(client, addr);

		if (r_data != w_data) {
			dev_err(&client->dev,
				"%s: verification failed (addr: 0x%x, w_data: 0x%x, r_data: 0x%x)\n",
				__func__, addr, w_data, r_data);
			retry_cnt--;
		} else
			break;
	}
}

static void fg_test_print(struct i2c_client *client)
{
	u8 data[2];
	u32 average_vcell;
	u16 w_data;
	u32 temp;
	u32 temp2;
	u16 reg_data;

	if (fg_i2c_read(client, AVR_VCELL_REG, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read VCELL\n", __func__);
		return;
	}

	w_data = (data[1]<<8) | data[0];

	temp = (w_data & 0xFFF) * 78125;
	average_vcell = temp / 1000000;

	temp = ((w_data & 0xF000) >> 4) * 78125;
	temp2 = temp / 1000000;
	average_vcell += (temp2 << 4);

	dev_info(&client->dev, "%s: AVG_VCELL(%d), data(0x%04x)\n", __func__,
		average_vcell, (data[1]<<8) | data[0]);

	reg_data = fg_read_register(client, FULLCAP_REG);
	dev_info(&client->dev, "%s: FULLCAP(%d), data(0x%04x)\n", __func__,
		reg_data/2, reg_data);

	reg_data = fg_read_register(client, REMCAP_REP_REG);
	dev_info(&client->dev, "%s: REMCAP_REP(%d), data(0x%04x)\n", __func__,
		reg_data/2, reg_data);

	reg_data = fg_read_register(client, REMCAP_MIX_REG);
	dev_info(&client->dev, "%s: REMCAP_MIX(%d), data(0x%04x)\n", __func__,
		reg_data/2, reg_data);

	reg_data = fg_read_register(client, REMCAP_AV_REG);
	dev_info(&client->dev, "%s: REMCAP_AV(%d), data(0x%04x)\n", __func__,
		reg_data/2, reg_data);
}

static void fg_periodic_read(struct i2c_client *client)
{
	u8 reg;
	int i;
	int data[0x10];
	char *str = NULL;

	str = kzalloc(sizeof(char)*1024, GFP_KERNEL);
	if (!str)
		return;

	for (i = 0; i < 16; i++) {
		for (reg = 0; reg < 0x10; reg++)
			data[reg] = fg_read_register(client, reg + i * 0x10);

		sprintf(str+strlen(str),
			"%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,",
			data[0x00], data[0x01], data[0x02], data[0x03],
			data[0x04], data[0x05], data[0x06], data[0x07]);
		sprintf(str+strlen(str),
			"%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,",
			data[0x08], data[0x09], data[0x0a], data[0x0b],
			data[0x0c], data[0x0d], data[0x0e], data[0x0f]);
		if (i == 4)
			i = 13;
	}

	dev_info(&client->dev, "%s", str);

	kfree(str);
}

static void fg_read_regs(struct i2c_client *client, char *str)
{
	int data = 0;
	u32 addr = 0;

	for (addr = 0; addr <= 0x4f; addr++) {
		data = fg_read_register(client, addr);
		sprintf(str+strlen(str), "0x%04x, ", data);
	}

	/* "#" considered as new line in application */
	sprintf(str+strlen(str), "#");

	for (addr = 0xe0; addr <= 0xff; addr++) {
		data = fg_read_register(client, addr);
		sprintf(str+strlen(str), "0x%04x, ", data);
	}
}

static int fg_read_vcell(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data[2];
	u32 vcell;
	u16 w_data;
	u32 temp;
	u32 temp2;

	if (fg_i2c_read(client, VCELL_REG, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read VCELL\n", __func__);
		return -1;
	}

	w_data = (data[1]<<8) | data[0];

	temp = (w_data & 0xFFF) * 78125;
	vcell = temp / 1000000;

	temp = ((w_data & 0xF000) >> 4) * 78125;
	temp2 = temp / 1000000;
	vcell += (temp2 << 4);

	if (!(fuelgauge->info.pr_cnt % PRINT_COUNT))
		dev_info(&client->dev, "%s: VCELL(%d), data(0x%04x)\n",
			__func__, vcell, (data[1]<<8) | data[0]);

	return vcell;
}

static int fg_read_vfocv(struct i2c_client *client)
{
	u8 data[2];
	u32 vfocv = 0;
	u16 w_data;
	u32 temp;
	u32 temp2;

	if (fg_i2c_read(client, VFOCV_REG, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read VFOCV\n", __func__);
		return -1;
	}

	w_data = (data[1]<<8) | data[0];

	temp = (w_data & 0xFFF) * 78125;
	vfocv = temp / 1000000;

	temp = ((w_data & 0xF000) >> 4) * 78125;
	temp2 = temp / 1000000;
	vfocv += (temp2 << 4);

	return vfocv;
}

static int fg_check_battery_present(struct i2c_client *client)
{
	u8 status_data[2];
	int ret = 1;

	/* 1. Check Bst bit */
	if (fg_i2c_read(client, STATUS_REG, status_data, 2) < 0) {
		dev_err(&client->dev,
			"%s: Failed to read STATUS_REG\n", __func__);
		return 0;
	}

	if (status_data[0] & (0x1 << 3)) {
		dev_info(&client->dev,
			"%s: addr(0x01), data(0x%04x)\n", __func__,
			(status_data[1]<<8) | status_data[0]);
		dev_info(&client->dev, "%s: battery is absent!!\n", __func__);
		ret = 0;
	}

	return ret;
}


static int fg_read_temp(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data[2];
	int temper = 0;
	int i;

	if (fg_check_battery_present(client)) {
		if (fg_i2c_read(client, TEMPERATURE_REG, data, 2) < 0) {
			dev_err(&client->dev,
				"%s: Failed to read TEMPERATURE_REG\n",
				__func__);
			return -1;
		}

		if (data[1]&(0x1 << 7)) {
			temper = ((~(data[1]))&0xFF)+1;
			temper *= (-1000);
		} else {
			temper = data[1] & 0x7f;
			temper *= 1000;
			temper += data[0] * 39 / 10;

			/* Adjust temperature */
			for (i = 0; i < TEMP_RANGE_MAX_NUM-1; i++) {
				if ((temper >= get_battery_data(fuelgauge).
					temp_adjust_table[i][RANGE]) &&
					(temper < get_battery_data(fuelgauge).
					temp_adjust_table[i+1][RANGE])) {
					temper = (temper *
						get_battery_data(fuelgauge).
						temp_adjust_table[i][SLOPE] /
						100) -
						get_battery_data(fuelgauge).
						temp_adjust_table[i][OFFSET];
				}
			}
			if (i == TEMP_RANGE_MAX_NUM-1)
				dev_dbg(&client->dev,
					"%s : No adjustment for temperature\n",
					__func__);
		}
	} else
		temper = 20000;

	if (!(fuelgauge->info.pr_cnt % PRINT_COUNT))
		dev_info(&client->dev, "%s: TEMPERATURE(%d), data(0x%04x)\n",
			__func__, temper, (data[1]<<8) | data[0]);

	return temper/100;
}

/* soc should be 0.1% unit */
static int fg_read_vfsoc(struct i2c_client *client)
{
	u8 data[2];
	int soc;

	if (fg_i2c_read(client, VFSOC_REG, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read VFSOC\n", __func__);
		return -1;
	}

	soc = ((data[1] * 100) + (data[0] * 100 / 256)) / 10;

	return min(soc, 1000);
}

/* soc should be 0.1% unit */
static int fg_read_avsoc(struct i2c_client *client)
{
	u8 data[2];
	int soc;

	if (fg_i2c_read(client, SOCAV_REG, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read AVSOC\n", __func__);
		return -1;
	}

	soc = ((data[1] * 100) + (data[0] * 100 / 256)) / 10;

	return min(soc, 1000);
}

/* soc should be 0.1% unit */
static int fg_read_soc(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data[2];
	int soc;

	if (fg_i2c_read(client, SOCREP_REG, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read SOCREP\n", __func__);
		return -1;
	}

	soc = ((data[1] * 100) + (data[0] * 100 / 256)) / 10;

	dev_dbg(&client->dev, "%s: raw capacity (%d)\n", __func__, soc);

	if (!(fuelgauge->info.pr_cnt % PRINT_COUNT))
		dev_dbg(&client->dev, "%s: raw capacity (%d), data(0x%04x)\n",
			__func__, soc, (data[1]<<8) | data[0]);

	return min(soc, 1000);
}

static int fg_read_current(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data1[2], data2[2];
	u32 temp, sign;
	s32 i_current;
	s32 avg_current;

	if (fg_i2c_read(client, CURRENT_REG, data1, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read CURRENT\n",
			__func__);
		return -1;
	}

	if (fg_i2c_read(client, AVG_CURRENT_REG, data2, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read AVERAGE CURRENT\n",
			__func__);
		return -1;
	}

	temp = ((data1[1]<<8) | data1[0]) & 0xFFFF;
	if (temp & (0x1 << 15)) {
		sign = NEGATIVE;
		temp = (~temp & 0xFFFF) + 1;
	} else
		sign = POSITIVE;

	/* 1.5625uV/0.01Ohm(Rsense) = 156.25uA */
	i_current = temp * 15625 / 100000;
	if (sign)
		i_current *= -1;

	temp = ((data2[1]<<8) | data2[0]) & 0xFFFF;
	if (temp & (0x1 << 15)) {
		sign = NEGATIVE;
		temp = (~temp & 0xFFFF) + 1;
	} else
		sign = POSITIVE;

	/* 1.5625uV/0.01Ohm(Rsense) = 156.25uA */
	avg_current = temp * 15625 / 100000;
	if (sign)
		avg_current *= -1;

	if (!(fuelgauge->info.pr_cnt++ % PRINT_COUNT)) {
		fg_test_print(client);
		dev_info(&client->dev, "%s: CURRENT(%dmA), AVG_CURRENT(%dmA)\n",
			__func__, i_current, avg_current);
		fuelgauge->info.pr_cnt = 1;
		/* Read max17050's all registers every 5 minute. */
		fg_periodic_read(client);
	}

	return i_current;
}

static int fg_read_avg_current(struct i2c_client *client)
{
	u8  data2[2];
	u32 temp, sign;
	s32 avg_current;

	if (fg_i2c_read(client, AVG_CURRENT_REG, data2, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read AVERAGE CURRENT\n",
			__func__);
		return -1;
	}

	temp = ((data2[1]<<8) | data2[0]) & 0xFFFF;
	if (temp & (0x1 << 15)) {
		sign = NEGATIVE;
		temp = (~temp & 0xFFFF) + 1;
	} else
		sign = POSITIVE;

	/* 1.5625uV/0.01Ohm(Rsense) = 156.25uA */
	avg_current = temp * 15625 / 100000;

	if (sign)
		avg_current *= -1;

	return avg_current;
}

int fg_reset_soc(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data[2];
	int vfocv, fullcap;

	/* delay for current stablization */
	msleep(500);

	dev_info(&client->dev,
		"%s: Before quick-start - VCELL(%d), VFOCV(%d), VfSOC(%d), RepSOC(%d)\n",
		__func__, fg_read_vcell(client), fg_read_vfocv(client),
		fg_read_vfsoc(client), fg_read_soc(client));
	dev_info(&client->dev,
		"%s: Before quick-start - current(%d), avg current(%d)\n",
		__func__, fg_read_current(client),
		fg_read_avg_current(client));

	if (!fuelgauge->pdata->check_jig_status()) {
		dev_info(&client->dev,
			"%s : Return by No JIG_ON signal\n", __func__);
		return 0;
	}

	fg_write_register(client, CYCLES_REG, 0);

	if (fg_i2c_read(client, MISCCFG_REG, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read MiscCFG\n", __func__);
		return -1;
	}

	data[1] |= (0x1 << 2);
	if (fg_i2c_write(client, MISCCFG_REG, data, 2) < 0) {
		dev_err(&client->dev,
			"%s: Failed to write MiscCFG\n", __func__);
		return -1;
	}

	msleep(250);
	fg_write_register(client, FULLCAP_REG,
		get_battery_data(fuelgauge).Capacity);
	msleep(500);

	dev_info(&client->dev,
		"%s: After quick-start - VCELL(%d), VFOCV(%d), VfSOC(%d), RepSOC(%d)\n",
		__func__, fg_read_vcell(client), fg_read_vfocv(client),
		fg_read_vfsoc(client), fg_read_soc(client));
	dev_info(&client->dev,
		"%s: After quick-start - current(%d), avg current(%d)\n",
		__func__, fg_read_current(client),
		fg_read_avg_current(client));
	fg_write_register(client, CYCLES_REG, 0x00a0);

/* P8 is not turned off by Quickstart @3.4V
 * (It's not a problem, depend on mode data)
 * Power off for factory test(File system, etc..) */
	vfocv = fg_read_vfocv(client);
	if (vfocv < POWER_OFF_VOLTAGE_LOW_MARGIN) {
		dev_info(&client->dev, "%s: Power off condition(%d)\n",
			__func__, vfocv);

		fullcap = fg_read_register(client, FULLCAP_REG);
		/* FullCAP * 0.009 */
		fg_write_register(client, REMCAP_REP_REG,
			(u16)(fullcap * 9 / 1000));
		msleep(200);
		dev_info(&client->dev, "%s: new soc=%d, vfocv=%d\n", __func__,
			fg_read_soc(client), vfocv);
	}

	dev_info(&client->dev,
		"%s: Additional step - VfOCV(%d), VfSOC(%d), RepSOC(%d)\n",
		__func__, fg_read_vfocv(client),
		fg_read_vfsoc(client), fg_read_soc(client));

	return 0;
}


int fg_reset_capacity_by_jig_connection(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);

	dev_info(&client->dev,
		"%s: DesignCap = Capacity - 1 (Jig Connection)\n", __func__);

	return fg_write_register(client, DESIGNCAP_REG,
		get_battery_data(fuelgauge).Capacity-1);
}

int fg_adjust_capacity(struct i2c_client *client)
{
	u8 data[2];

	data[0] = 0;
	data[1] = 0;

	/* 1. Write RemCapREP(05h)=0; */
	if (fg_i2c_write(client, REMCAP_REP_REG, data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to write RemCap_REP\n",
			__func__);
		return -1;
	}
	msleep(200);

	dev_info(&client->dev, "%s: After adjust - RepSOC(%d)\n", __func__,
		fg_read_soc(client));

	return 0;
}

void fg_low_batt_compensation(struct i2c_client *client, u32 level)
{
	int read_val;
	u32 temp;

	dev_info(&client->dev, "%s: Adjust SOCrep to %d!!\n",
		__func__, level);

	read_val = fg_read_register(client, FULLCAP_REG);
	if (read_val < 0)
		return;

	if (read_val > 2)	/* 3% compensation */
		/* RemCapREP (05h) = FullCap(10h) x 0.0301 */
		temp = read_val * (level*100 + 1) / 10000;
	else				/* 1% compensation */
		/* RemCapREP (05h) = FullCap(10h) x 0.0090 */
		temp = read_val * (level*90) / 10000;
	fg_write_register(client, REMCAP_REP_REG, (u16)temp);
}

static void fg_read_model_data(struct i2c_client *client)
{
	u16 data0[16], data1[16], data2[16];
	int i;
	int relock_check;

	dev_info(&client->dev, "[FG_Model] ");

	/* Unlock model access */
	fg_write_register(client, 0x62, 0x0059);
	fg_write_register(client, 0x63, 0x00C4);

	/* Read model data */
	fg_read_16register(client, 0x80, data0);
	fg_read_16register(client, 0x90, data1);
	fg_read_16register(client, 0xa0, data2);

	/* Print model data */
	for (i = 0; i < 16; i++)
		dev_info(&client->dev, "0x%04x, ", data0[i]);

	for (i = 0; i < 16; i++)
		dev_info(&client->dev, "0x%04x, ", data1[i]);

	for (i = 0; i < 16; i++) {
		if (i == 15)
			dev_info(&client->dev, "0x%04x", data2[i]);
		else
			dev_info(&client->dev, "0x%04x, ", data2[i]);
	}

	do {
		relock_check = 0;
		/* Lock model access */
		fg_write_register(client, 0x62, 0x0000);
		fg_write_register(client, 0x63, 0x0000);

		/* Read model data again */
		fg_read_16register(client, 0x80, data0);
		fg_read_16register(client, 0x90, data1);
		fg_read_16register(client, 0xa0, data2);

		for (i = 0; i < 16; i++) {
			if (data0[i] || data1[i] || data2[i]) {
				dev_dbg(&client->dev,
					"%s: data is non-zero, lock again!!\n",
					__func__);
				relock_check = 1;
			}
		}
	} while (relock_check);

}

static int fg_check_status_reg(struct i2c_client *client)
{
	u8 status_data[2];
	int ret = 0;

	/* 1. Check Smn was generatedread */
	if (fg_i2c_read(client, STATUS_REG, status_data, 2) < 0) {
		dev_err(&client->dev, "%s: Failed to read STATUS_REG\n",
			__func__);
		return -1;
	}
	dev_info(&client->dev, "%s: addr(0x00), data(0x%04x)\n", __func__,
		(status_data[1]<<8) | status_data[0]);

	if (status_data[1] & (0x1 << 2))
		ret = 1;

	/* 2. clear Status reg */
	status_data[1] = 0;
	if (fg_i2c_write(client, STATUS_REG, status_data, 2) < 0) {
		dev_info(&client->dev, "%s: Failed to write STATUS_REG\n",
			__func__);
		return -1;
	}

	return ret;
}

int get_fuelgauge_value(struct i2c_client *client, int data)
{
	int ret;

	switch (data) {
	case FG_LEVEL:
		ret = fg_read_soc(client);
		break;

	case FG_TEMPERATURE:
		ret = fg_read_temp(client);
		break;

	case FG_VOLTAGE:
		ret = fg_read_vcell(client);
		break;

	case FG_CURRENT:
		ret = fg_read_current(client);
		break;

	case FG_CURRENT_AVG:
		ret = fg_read_avg_current(client);
		break;

	case FG_CHECK_STATUS:
		ret = fg_check_status_reg(client);
		break;

	case FG_VF_SOC:
		ret = fg_read_vfsoc(client);
		break;

	case FG_AV_SOC:
		ret = fg_read_avsoc(client);
		break;

	default:
		ret = -1;
		break;
	}

	return ret;
}

int fg_alert_init(struct i2c_client *client, int soc)
{
	u8 misccgf_data[2];
	u8 salrt_data[2];
	u8 config_data[2];
	u8 valrt_data[2];
	u8 talrt_data[2];
	u16 read_data = 0;

	/* Using RepSOC */
	if (fg_i2c_read(client, MISCCFG_REG, misccgf_data, 2) < 0) {
		dev_err(&client->dev,
			"%s: Failed to read MISCCFG_REG\n", __func__);
		return -1;
	}
	misccgf_data[0] = misccgf_data[0] & ~(0x03);

	if (fg_i2c_write(client, MISCCFG_REG, misccgf_data, 2) < 0) {
		dev_info(&client->dev,
			"%s: Failed to write MISCCFG_REG\n", __func__);
		return -1;
	}

	/* SALRT Threshold setting */
	salrt_data[1] = 0xff;
	salrt_data[0] = soc;
	if (fg_i2c_write(client, SALRT_THRESHOLD_REG, salrt_data, 2) < 0) {
		dev_info(&client->dev,
			"%s: Failed to write SALRT_THRESHOLD_REG\n", __func__);
		return -1;
	}

	/* Reset VALRT Threshold setting (disable) */
	valrt_data[1] = 0xFF;
	valrt_data[0] = 0x00;
	if (fg_i2c_write(client, VALRT_THRESHOLD_REG, valrt_data, 2) < 0) {
		dev_info(&client->dev,
			"%s: Failed to write VALRT_THRESHOLD_REG\n", __func__);
		return -1;
	}

	read_data = fg_read_register(client, (u8)VALRT_THRESHOLD_REG);
	if (read_data != 0xff00)
		dev_err(&client->dev,
			"%s: VALRT_THRESHOLD_REG is not valid (0x%x)\n",
			__func__, read_data);

	/* Reset TALRT Threshold setting (disable) */
	talrt_data[1] = 0x7F;
	talrt_data[0] = 0x80;
	if (fg_i2c_write(client, TALRT_THRESHOLD_REG, talrt_data, 2) < 0) {
		dev_info(&client->dev,
			"%s: Failed to write TALRT_THRESHOLD_REG\n", __func__);
		return -1;
	}

	read_data = fg_read_register(client, (u8)TALRT_THRESHOLD_REG);
	if (read_data != 0x7f80)
		dev_err(&client->dev,
			"%s: TALRT_THRESHOLD_REG is not valid (0x%x)\n",
			__func__, read_data);

	mdelay(100);

	/* Enable SOC alerts */
	if (fg_i2c_read(client, CONFIG_REG, config_data, 2) < 0) {
		dev_err(&client->dev,
			"%s: Failed to read CONFIG_REG\n", __func__);
		return -1;
	}
	config_data[0] = config_data[0] | (0x1 << 2);

	if (fg_i2c_write(client, CONFIG_REG, config_data, 2) < 0) {
		dev_info(&client->dev,
			"%s: Failed to write CONFIG_REG\n", __func__);
		return -1;
	}

	return 1;
}

void fg_fullcharged_compensation(struct i2c_client *client,
		u32 is_recharging, bool pre_update)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	static int new_fullcap_data;

	dev_info(&client->dev, "%s: is_recharging(%d), pre_update(%d)\n",
		__func__, is_recharging, pre_update);

	new_fullcap_data =
		fg_read_register(client, FULLCAP_REG);
	if (new_fullcap_data < 0)
		new_fullcap_data = get_battery_data(fuelgauge).Capacity;

	/* compare with initial capacity */
	if (new_fullcap_data >
		(get_battery_data(fuelgauge).Capacity * 110 / 100)) {
		dev_info(&client->dev,
			"%s: [Case 1] capacity = 0x%04x, NewFullCap = 0x%04x\n",
			__func__, get_battery_data(fuelgauge).Capacity,
			new_fullcap_data);

		new_fullcap_data =
			(get_battery_data(fuelgauge).Capacity * 110) / 100;

		fg_write_register(client, REMCAP_REP_REG,
			(u16)(new_fullcap_data));
		fg_write_register(client, FULLCAP_REG,
			(u16)(new_fullcap_data));
	} else if (new_fullcap_data <
		(get_battery_data(fuelgauge).Capacity * 50 / 100)) {
		dev_info(&client->dev,
			"%s: [Case 5] capacity = 0x%04x, NewFullCap = 0x%04x\n",
			__func__, get_battery_data(fuelgauge).Capacity,
			new_fullcap_data);

		new_fullcap_data =
			(get_battery_data(fuelgauge).Capacity * 50) / 100;

		fg_write_register(client, REMCAP_REP_REG,
			(u16)(new_fullcap_data));
		fg_write_register(client, FULLCAP_REG,
			(u16)(new_fullcap_data));
	} else {
	/* compare with previous capacity */
		if (new_fullcap_data >
			(fuelgauge->info.previous_fullcap * 110 / 100)) {
			dev_info(&client->dev,
				"%s: [Case 2] previous_fullcap = 0x%04x, NewFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_fullcap,
				new_fullcap_data);

			new_fullcap_data =
				(fuelgauge->info.previous_fullcap * 110) / 100;

			fg_write_register(client, REMCAP_REP_REG,
				(u16)(new_fullcap_data));
			fg_write_register(client, FULLCAP_REG,
				(u16)(new_fullcap_data));
		} else if (new_fullcap_data <
			(fuelgauge->info.previous_fullcap * 90 / 100)) {
			dev_info(&client->dev,
				"%s: [Case 3] previous_fullcap = 0x%04x, NewFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_fullcap,
				new_fullcap_data);

			new_fullcap_data =
				(fuelgauge->info.previous_fullcap * 90) / 100;

			fg_write_register(client, REMCAP_REP_REG,
				(u16)(new_fullcap_data));
			fg_write_register(client, FULLCAP_REG,
				(u16)(new_fullcap_data));
		} else {
			dev_info(&client->dev,
				"%s: [Case 4] previous_fullcap = 0x%04x, NewFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_fullcap,
				new_fullcap_data);
		}
	}

	/* 4. Write RepSOC(06h)=100%; */
	fg_write_register(client, SOCREP_REG, (u16)(0x64 << 8));

	/* 5. Write MixSOC(0Dh)=100%; */
	fg_write_register(client, SOCMIX_REG, (u16)(0x64 << 8));

	/* 6. Write AVSOC(0Eh)=100%; */
	fg_write_register(client, SOCAV_REG, (u16)(0x64 << 8));

	/* if pre_update case, skip updating PrevFullCAP value. */
	if (!pre_update)
		fuelgauge->info.previous_fullcap =
			fg_read_register(client, FULLCAP_REG);

	dev_info(&client->dev,
		"%s: (A) FullCap = 0x%04x, RemCap = 0x%04x\n", __func__,
		fg_read_register(client, FULLCAP_REG),
		fg_read_register(client, REMCAP_REP_REG));

	fg_periodic_read(client);
}

void fg_check_vf_fullcap_range(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	static int new_vffullcap;
	bool is_vffullcap_changed = true;

	if (fuelgauge->pdata->check_jig_status())
		fg_reset_capacity_by_jig_connection(client);

	new_vffullcap = fg_read_register(client, FULLCAP_NOM_REG);
	if (new_vffullcap < 0)
		new_vffullcap = get_battery_data(fuelgauge).Capacity;

	/* compare with initial capacity */
	if (new_vffullcap >
		(get_battery_data(fuelgauge).Capacity * 110 / 100)) {
		dev_info(&client->dev,
			"%s: [Case 1] capacity = 0x%04x, NewVfFullCap = 0x%04x\n",
			__func__, get_battery_data(fuelgauge).Capacity,
			new_vffullcap);

		new_vffullcap =
			(get_battery_data(fuelgauge).Capacity * 110) / 100;

		fg_write_register(client, DQACC_REG,
			(u16)(new_vffullcap / 4));
		fg_write_register(client, DPACC_REG, (u16)0x3200);
	} else if (new_vffullcap <
		(get_battery_data(fuelgauge).Capacity * 50 / 100)) {
		dev_info(&client->dev,
			"%s: [Case 5] capacity = 0x%04x, NewVfFullCap = 0x%04x\n",
			__func__, get_battery_data(fuelgauge).Capacity,
			new_vffullcap);

		new_vffullcap =
			(get_battery_data(fuelgauge).Capacity * 50) / 100;

		fg_write_register(client, DQACC_REG,
			(u16)(new_vffullcap / 4));
		fg_write_register(client, DPACC_REG, (u16)0x3200);
	} else {
	/* compare with previous capacity */
		if (new_vffullcap >
			(fuelgauge->info.previous_vffullcap * 110 / 100)) {
			dev_info(&client->dev,
				"%s: [Case 2] previous_vffullcap = 0x%04x, NewVfFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_vffullcap,
				new_vffullcap);

			new_vffullcap =
				(fuelgauge->info.previous_vffullcap * 110) /
				100;

			fg_write_register(client, DQACC_REG,
				(u16)(new_vffullcap / 4));
			fg_write_register(client, DPACC_REG, (u16)0x3200);
		} else if (new_vffullcap <
			(fuelgauge->info.previous_vffullcap * 90 / 100)) {
			dev_info(&client->dev,
				"%s: [Case 3] previous_vffullcap = 0x%04x, NewVfFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_vffullcap,
				new_vffullcap);

			new_vffullcap =
				(fuelgauge->info.previous_vffullcap * 90) / 100;

			fg_write_register(client, DQACC_REG,
				(u16)(new_vffullcap / 4));
			fg_write_register(client, DPACC_REG, (u16)0x3200);
		} else {
			dev_info(&client->dev,
				"%s: [Case 4] previous_vffullcap = 0x%04x, NewVfFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_vffullcap,
				new_vffullcap);
			is_vffullcap_changed = false;
		}
	}

	/* delay for register setting (dQacc, dPacc) */
	if (is_vffullcap_changed)
		msleep(300);

	fuelgauge->info.previous_vffullcap =
		fg_read_register(client, FULLCAP_NOM_REG);

	if (is_vffullcap_changed)
		dev_info(&client->dev,
			"%s : VfFullCap(0x%04x), dQacc(0x%04x), dPacc(0x%04x)\n",
			__func__,
			fg_read_register(client, FULLCAP_NOM_REG),
			fg_read_register(client, DQACC_REG),
			fg_read_register(client, DPACC_REG));

}

void fg_set_full_charged(struct i2c_client *client)
{
	dev_info(&client->dev, "[FG_Set_Full] (B) FullCAP(%d), RemCAP(%d)\n",
		(fg_read_register(client, FULLCAP_REG)/2),
		(fg_read_register(client, REMCAP_REP_REG)/2));

	fg_write_register(client, FULLCAP_REG,
		(u16)fg_read_register(client, REMCAP_REP_REG));

	dev_info(&client->dev, "[FG_Set_Full] (A) FullCAP(%d), RemCAP(%d)\n",
		(fg_read_register(client, FULLCAP_REG)/2),
		(fg_read_register(client, REMCAP_REP_REG)/2));
}

static void display_low_batt_comp_cnt(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);

	pr_info("Check Array(%s): [%d, %d], [%d, %d], ",
			get_battery_data(fuelgauge).type_str,
			fuelgauge->info.low_batt_comp_cnt[0][0],
			fuelgauge->info.low_batt_comp_cnt[0][1],
			fuelgauge->info.low_batt_comp_cnt[1][0],
			fuelgauge->info.low_batt_comp_cnt[1][1]);
	pr_info("[%d, %d], [%d, %d], [%d, %d]\n",
			fuelgauge->info.low_batt_comp_cnt[2][0],
			fuelgauge->info.low_batt_comp_cnt[2][1],
			fuelgauge->info.low_batt_comp_cnt[3][0],
			fuelgauge->info.low_batt_comp_cnt[3][1],
			fuelgauge->info.low_batt_comp_cnt[4][0],
			fuelgauge->info.low_batt_comp_cnt[4][1]);
}

static void add_low_batt_comp_cnt(struct i2c_client *client,
				int range, int level)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	int i;
	int j;

	/* Increase the requested count value, and reset others. */
	fuelgauge->info.low_batt_comp_cnt[range-1][level/2]++;

	for (i = 0; i < LOW_BATT_COMP_RANGE_NUM; i++) {
		for (j = 0; j < LOW_BATT_COMP_LEVEL_NUM; j++) {
			if (i == range-1 && j == level/2)
				continue;
			else
				fuelgauge->info.low_batt_comp_cnt[i][j] = 0;
		}
	}
}

void prevent_early_poweroff(struct i2c_client *client,
	int vcell, int *fg_soc)
{
	int soc = 0;
	int read_val;

	soc = get_fuelgauge_value(client, FG_LEVEL);

	if (soc > POWER_OFF_SOC_HIGH_MARGIN)
		return;

	dev_info(&client->dev, "%s: soc=%d%%, vcell=%d\n", __func__,
		soc, vcell);

	if (vcell > POWER_OFF_VOLTAGE_HIGH_MARGIN) {
		read_val = fg_read_register(client, FULLCAP_REG);
		/* FullCAP * 0.013 */
		fg_write_register(client, REMCAP_REP_REG,
		(u16)(read_val * 13 / 1000));
		msleep(200);
		*fg_soc = fg_read_soc(client);
		dev_info(&client->dev, "%s : new soc=%d, vcell=%d\n",
			__func__, *fg_soc, vcell);
	}
}

void reset_low_batt_comp_cnt(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);

	memset(fuelgauge->info.low_batt_comp_cnt, 0,
		sizeof(fuelgauge->info.low_batt_comp_cnt));
}

static int check_low_batt_comp_condition(
				struct i2c_client *client, int *nLevel)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	int i;
	int j;
	int ret = 0;

	for (i = 0; i < LOW_BATT_COMP_RANGE_NUM; i++) {
		for (j = 0; j < LOW_BATT_COMP_LEVEL_NUM; j++) {
			if (fuelgauge->info.low_batt_comp_cnt[i][j] >=
				MAX_LOW_BATT_CHECK_CNT) {
				display_low_batt_comp_cnt(client);
				ret = 1;
				*nLevel = j*2 + 1;
				break;
			}
		}
	}

	return ret;
}

static int get_low_batt_threshold(struct i2c_client *client,
				int range, int nCurrent, int level)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	int ret = 0;

	ret = get_battery_data(fuelgauge).low_battery_table[range][OFFSET] +
		((nCurrent *
		get_battery_data(fuelgauge).low_battery_table[range][SLOPE]) /
		1000);

	return ret;
}

int low_batt_compensation(struct i2c_client *client,
		int fg_soc, int fg_vcell, int fg_current)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	int fg_avg_current = 0;
	int fg_min_current = 0;
	int new_level = 0;
	int i, table_size;

	/* Not charging, Under low battery comp voltage */
	if (fg_vcell <= get_battery_data(fuelgauge).low_battery_comp_voltage) {
		fg_avg_current = fg_read_avg_current(client);
		fg_min_current = min(fg_avg_current, fg_current);

		table_size =
			sizeof(get_battery_data(fuelgauge).low_battery_table) /
			(sizeof(s16)*TABLE_MAX);

	for (i = 1; i < CURRENT_RANGE_MAX_NUM; i++) {
		if ((fg_min_current >= get_battery_data(fuelgauge).
			low_battery_table[i-1][RANGE]) &&
			(fg_min_current < get_battery_data(fuelgauge).
			low_battery_table[i][RANGE])) {
			if (fg_soc >= 2 && fg_vcell <
				get_low_batt_threshold(client,
				i, fg_min_current, 1)) {
				add_low_batt_comp_cnt(
					client, i, 1);
			} else {
				reset_low_batt_comp_cnt(client);
			}
		}
	}

		if (check_low_batt_comp_condition(client, &new_level)) {
			fg_low_batt_compensation(client, new_level);
			reset_low_batt_comp_cnt(client);
		}

		/* if compensation finished, then read SOC again!!*/
		dev_info(&client->dev,
			"%s: MIN_CURRENT(%d), AVG_CURRENT(%d), CURRENT(%d), SOC(%d), VCELL(%d)\n",
			__func__, fg_min_current, fg_avg_current,
			fg_current, fg_soc, fg_vcell);
		/* Do not update soc right after low battery compensation */
		/* to prevent from powering-off suddenly */
		dev_info(&client->dev,
			"%s: SOC is set to %d\n",
			__func__, fg_read_soc(client));
	}

	/* Prevent power off over 3500mV */
	prevent_early_poweroff(client, fg_vcell, &fg_soc);

	return fg_soc;
}

static bool is_booted_in_low_battery(struct i2c_client *client)
{
	int fg_vcell = get_fuelgauge_value(client, FG_VOLTAGE);
	int fg_current = get_fuelgauge_value(client, FG_CURRENT);
	int threshold = 0;

	threshold = 3300 + ((fg_current * 17) / 100);

	if (fg_vcell <= threshold)
		return true;
	else
		return false;
}

static bool fuelgauge_recovery_handler(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	int current_soc;
	int avsoc;
	int temperature;

	if (fuelgauge->info.soc > LOW_BATTERY_SOC_REDUCE_UNIT) {
		dev_err(&client->dev,
			"%s: Reduce the Reported SOC by 1%%\n",
			__func__);
		current_soc =
			get_fuelgauge_value(client, FG_LEVEL);

		if (current_soc) {
			dev_info(&client->dev,
				"%s: Returning to Normal discharge path\n",
				__func__);
			dev_info(&client->dev,
				"%s: Actual SOC(%d) non-zero\n",
				__func__, current_soc);
			fuelgauge->info.is_low_batt_alarm = false;
		} else {
			temperature =
				get_fuelgauge_value(client, FG_TEMPERATURE);
			avsoc =
				get_fuelgauge_value(client, FG_AV_SOC);

			if ((fuelgauge->info.soc > avsoc) ||
				(temperature < 0)) {
				fuelgauge->info.soc -=
					LOW_BATTERY_SOC_REDUCE_UNIT;
				dev_err(&client->dev,
					"%s: New Reduced RepSOC (%d)\n",
					__func__, fuelgauge->info.soc);
			} else
				dev_info(&client->dev,
					"%s: Waiting for recovery (AvSOC:%d)\n",
					__func__, avsoc);
		}
	}

	return fuelgauge->info.is_low_batt_alarm;
}

static int get_fuelgauge_soc(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	union power_supply_propval value;
	int fg_soc;
	int fg_vfsoc;
	int fg_vcell;
	int fg_current;
	int avg_current;
	ktime_t	current_time;
	struct timespec ts;
	int fullcap_check_interval;

	if (fuelgauge->info.is_low_batt_alarm)
		if (fuelgauge_recovery_handler(client))
			goto return_soc;

	current_time = alarm_get_elapsed_realtime();
	ts = ktime_to_timespec(current_time);

	/* check fullcap range */
	fullcap_check_interval =
		(ts.tv_sec - fuelgauge->info.fullcap_check_interval);
	if (fullcap_check_interval >
		VFFULLCAP_CHECK_INTERVAL) {
		dev_info(&client->dev,
			"%s: check fullcap range (interval:%d)\n",
			__func__, fullcap_check_interval);
		fg_check_vf_fullcap_range(client);
		fuelgauge->info.fullcap_check_interval = ts.tv_sec;
	}

	fg_soc = get_fuelgauge_value(client, FG_LEVEL);
	if (fg_soc < 0) {
		dev_info(&client->dev, "Can't read soc!!!");
		fg_soc = fuelgauge->info.soc;
	}

	if (fuelgauge->info.low_batt_boot_flag) {
		fg_soc = 0;

		if (fuelgauge->pdata->check_cable_callback() !=
			POWER_SUPPLY_TYPE_BATTERY &&
			!is_booted_in_low_battery(client)) {
			fg_adjust_capacity(client);
			fuelgauge->info.low_batt_boot_flag = 0;
		}

		if (fuelgauge->pdata->check_cable_callback() ==
			POWER_SUPPLY_TYPE_BATTERY)
			fuelgauge->info.low_batt_boot_flag = 0;
	}

	fg_vcell = get_fuelgauge_value(client, FG_VOLTAGE);
	fg_current = get_fuelgauge_value(client, FG_CURRENT);
	avg_current = get_fuelgauge_value(client, FG_CURRENT_AVG);
	fg_vfsoc = get_fuelgauge_value(client, FG_VF_SOC);

	psy_do_property("battery", get,
		POWER_SUPPLY_PROP_CHARGE_TYPE, value);

	/* Algorithm for reducing time to fully charged (from MAXIM) */
	if (value.intval != SEC_BATTERY_CHARGING_NONE &&
		value.intval != SEC_BATTERY_CHARGING_RECHARGING &&
		fuelgauge->cable_type != POWER_SUPPLY_TYPE_USB &&
		/* Skip when first check after boot up */
		!fuelgauge->info.is_first_check &&
		(fg_vfsoc > VFSOC_FOR_FULLCAP_LEARNING &&
		(fg_current > LOW_CURRENT_FOR_FULLCAP_LEARNING &&
		fg_current < HIGH_CURRENT_FOR_FULLCAP_LEARNING) &&
		(avg_current > LOW_AVGCURRENT_FOR_FULLCAP_LEARNING &&
		avg_current < HIGH_AVGCURRENT_FOR_FULLCAP_LEARNING))) {

		if (fuelgauge->info.full_check_flag == 2) {
			dev_info(&client->dev,
				"%s: force fully charged SOC !! (%d)",
				__func__, fuelgauge->info.full_check_flag);
			fg_set_full_charged(client);
			fg_soc = get_fuelgauge_value(client, FG_LEVEL);
		} else if (fuelgauge->info.full_check_flag < 2)
			dev_info(&client->dev,
				"%s: full_check_flag (%d)",
				__func__, fuelgauge->info.full_check_flag);

		/* prevent overflow */
		if (fuelgauge->info.full_check_flag++ > 10000)
			fuelgauge->info.full_check_flag = 3;
	} else
		fuelgauge->info.full_check_flag = 0;

	/*  Checks vcell level and tries to compensate SOC if needed.*/
	/*  If jig cable is connected, then skip low batt compensation check. */
	if (!fuelgauge->pdata->check_jig_status() &&
		value.intval == SEC_BATTERY_CHARGING_NONE)
		fg_soc = low_batt_compensation(
			client, fg_soc, fg_vcell, fg_current);

	if (fuelgauge->info.is_first_check)
		fuelgauge->info.is_first_check = false;

	fuelgauge->info.soc = fg_soc;

return_soc:
	dev_dbg(&client->dev, "%s: soc(%d), low_batt_alarm(%d)\n",
		__func__, fuelgauge->info.soc,
		fuelgauge->info.is_low_batt_alarm);

	return fg_soc;
}

static void full_comp_work_handler(struct work_struct *work)
{
	struct sec_fg_info *fg_info =
		container_of(work, struct sec_fg_info, full_comp_work.work);
	struct sec_fuelgauge_info *fuelgauge =
		container_of(fg_info, struct sec_fuelgauge_info, info);
	int avg_current;
	union power_supply_propval value;

	avg_current = get_fuelgauge_value(fuelgauge->client, FG_CURRENT_AVG);
	psy_do_property("battery", get,
		POWER_SUPPLY_PROP_CHARGE_TYPE, value);

	if (avg_current >= 25) {
		cancel_delayed_work(&fuelgauge->info.full_comp_work);
		schedule_delayed_work(&fuelgauge->info.full_comp_work, 100);
	} else {
		dev_info(&fuelgauge->client->dev,
			"%s: full charge compensation start (avg_current %d)\n",
			__func__, avg_current);
		fg_fullcharged_compensation(fuelgauge->client,
			(int)(value.intval ==
			SEC_BATTERY_CHARGING_RECHARGING), false);
	}
}

bool sec_hal_fg_init(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge =
				i2c_get_clientdata(client);
	ktime_t	current_time;
	struct timespec ts;

	current_time = alarm_get_elapsed_realtime();
	ts = ktime_to_timespec(current_time);

	fuelgauge->info.fullcap_check_interval = ts.tv_sec;

	fuelgauge->info.is_low_batt_alarm = false;
	fuelgauge->info.is_first_check = true;

	/* Init parameters to prevent wrong compensation. */
	fuelgauge->info.previous_fullcap =
		fg_read_register(client, FULLCAP_REG);
	fuelgauge->info.previous_vffullcap =
		fg_read_register(client, FULLCAP_NOM_REG);

	fg_read_model_data(client);
	fg_periodic_read(client);

	if (fuelgauge->pdata->check_cable_callback() !=
		POWER_SUPPLY_TYPE_BATTERY &&
		is_booted_in_low_battery(client))
		fuelgauge->info.low_batt_boot_flag = 1;

	if (fuelgauge->pdata->check_jig_status())
		fg_reset_capacity_by_jig_connection(client);

	INIT_DELAYED_WORK(&fuelgauge->info.full_comp_work,
		full_comp_work_handler);

	return true;
}

bool sec_hal_fg_suspend(struct i2c_client *client)
{
	return true;
}

bool sec_hal_fg_resume(struct i2c_client *client)
{
	return true;
}

bool sec_hal_fg_fuelalert_init(struct i2c_client *client, int soc)
{
	if (fg_alert_init(client, soc) > 0)
		return true;
	else
		return false;
}

bool sec_hal_fg_is_fuelalerted(struct i2c_client *client)
{
	if (get_fuelgauge_value(client, FG_CHECK_STATUS) > 0)
		return true;
	else
		return false;
}

bool sec_hal_fg_fuelalert_process(void *irq_data, bool is_fuel_alerted)
{
	struct sec_fuelgauge_info *fuelgauge =
		(struct sec_fuelgauge_info *)irq_data;
	union power_supply_propval value;
	int overcurrent_limit_in_soc;
	int current_soc =
		get_fuelgauge_value(fuelgauge->client, FG_LEVEL);

	if (fuelgauge->info.soc <= STABLE_LOW_BATTERY_DIFF)
		overcurrent_limit_in_soc = STABLE_LOW_BATTERY_DIFF_LOWBATT;
	else
		overcurrent_limit_in_soc = STABLE_LOW_BATTERY_DIFF;

	if ((fuelgauge->info.soc - current_soc) >
		overcurrent_limit_in_soc) {
		dev_info(&fuelgauge->client->dev,
			"%s: Abnormal Current Consumption jump by %d units\n",
			__func__, ((fuelgauge->info.soc - current_soc)));
		dev_info(&fuelgauge->client->dev,
			"%s: Last Reported SOC (%d).\n",
			__func__, fuelgauge->info.soc);

		fuelgauge->info.is_low_batt_alarm = true;

		if (fuelgauge->info.soc >=
			LOW_BATTERY_SOC_REDUCE_UNIT)
			return true;
	}

	psy_do_property("battery", get,
		POWER_SUPPLY_PROP_CHARGE_TYPE, value);

	if (value.intval ==
			SEC_BATTERY_CHARGING_NONE) {
		dev_err(&fuelgauge->client->dev,
			"Set battery level as 0, power off.\n");
		fuelgauge->info.soc = 0;
		value.intval = 0;
		psy_do_property("battery", set,
			POWER_SUPPLY_PROP_CAPACITY, value);
	}

	return true;
}

bool sec_hal_fg_full_charged(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge =
				i2c_get_clientdata(client);
	union power_supply_propval value;

	psy_do_property("battery", get,
		POWER_SUPPLY_PROP_CHARGE_TYPE, value);

	/* full charge compensation algorithm by MAXIM */
	fg_fullcharged_compensation(client,
		(int)(value.intval == SEC_BATTERY_CHARGING_RECHARGING), true);

	cancel_delayed_work(&fuelgauge->info.full_comp_work);
	schedule_delayed_work(&fuelgauge->info.full_comp_work, 100);

	return false;
}

bool sec_hal_fg_reset(struct i2c_client *client)
{
	if (!fg_reset_soc(client))
		return true;
	else
		return false;
}

bool sec_hal_fg_get_property(struct i2c_client *client,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	switch (psp) {
		/* Cell voltage (VCELL, mV) */
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_fuelgauge_value(client, FG_VOLTAGE);
		break;
		/* Additional Voltage Information (mV) */
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		switch (val->intval) {
		case SEC_BATTEY_VOLTAGE_AVERAGE:
			val->intval = 0;
			break;
		case SEC_BATTEY_VOLTAGE_OCV:
			val->intval = fg_read_vfocv(client);
			break;
		}
		break;
		/* Current (mA) */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = get_fuelgauge_value(client, FG_CURRENT);
		break;
		/* Average Current (mA) */
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = get_fuelgauge_value(client, FG_CURRENT_AVG);
		break;
		/* SOC (%) */
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_fuelgauge_soc(client);
		break;
		/* Battery Temperature */
	case POWER_SUPPLY_PROP_TEMP:
		/* Target Temperature */
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		val->intval = get_fuelgauge_value(client, FG_TEMPERATURE);
		break;
	default:
		return false;
	}
	return true;
}

bool sec_hal_fg_set_property(struct i2c_client *client,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	struct sec_fuelgauge_info *fuelgauge =
				i2c_get_clientdata(client);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval != POWER_SUPPLY_TYPE_BATTERY) {
			if (fuelgauge->info.is_low_batt_alarm) {
				dev_info(&client->dev,
					"%s: Reset low_batt_alarm\n",
					__func__);
				fuelgauge->info.is_low_batt_alarm = false;
			}

			reset_low_batt_comp_cnt(client);
		}
		break;
		/* Battery Temperature */
	case POWER_SUPPLY_PROP_TEMP:
		/* Target Temperature */
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		break;
	default:
		return false;
	}
	return true;
}

ssize_t sec_hal_fg_show_attrs(struct device *dev,
				const ptrdiff_t offset, char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sec_fuelgauge_info *fg =
		container_of(psy, struct sec_fuelgauge_info, psy_fg);
	int i = 0;
	char *str = NULL;

	switch (offset) {
/*	case FG_REG: */
/*		break; */
	case FG_DATA:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%02x%02x\n",
			fg->reg_data[1], fg->reg_data[0]);
		break;
	case FG_REGS:
		str = kzalloc(sizeof(char)*1024, GFP_KERNEL);
		if (!str)
			return -ENOMEM;

		fg_read_regs(fg->client, str);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n",
			str);

		kfree(str);
		break;
	default:
		i = -EINVAL;
		break;
	}

	return i;
}

ssize_t sec_hal_fg_store_attrs(struct device *dev,
				const ptrdiff_t offset,
				const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sec_fuelgauge_info *fg =
		container_of(psy, struct sec_fuelgauge_info, psy_fg);
	int ret = 0;
	int x = 0;

	switch (offset) {
	case FG_REG:
		if (sscanf(buf, "%x\n", &x) == 1) {
			fg->reg_addr = x;
			fg_i2c_read(fg->client,
				fg->reg_addr, fg->reg_data, 2);
			dev_dbg(&fg->client->dev,
				"%s: (read) addr = 0x%x, data = 0x%02x%02x\n",
				 __func__, fg->reg_addr,
				 fg->reg_data[1], fg->reg_data[0]);
			ret = count;
		}
		break;
	case FG_DATA:
		if (sscanf(buf, "%x\n", &x) == 1) {
			dev_dbg(&fg->client->dev,
				"%s: (write) addr = 0x%x, data = 0x%02x%02x\n",
				__func__, fg->reg_addr, x);
			fg_write_and_verify_register(fg->client,
				fg->reg_addr, (u16)x);
			ret = count;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
#endif

