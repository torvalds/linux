/*
 *  max17042_fuelgauge.c
 *  Samsung MAX17042 Fuel Gauge Driver
 *
 *  Copyright (C) 2012 Samsung Electronics
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/battery/sec_fuelgauge.h>

#ifdef CONFIG_FUELGAUGE_MAX17042_VOLTAGE_TRACKING
static int max17042_write_reg(struct i2c_client *client, int reg, u8 *buf)
{
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, reg, 2, buf);

	if (ret < 0)
		pr_err("%s: Error(%d)\n", __func__, ret);

	return ret;
}

static int max17042_read_reg(struct i2c_client *client, int reg, u8 *buf)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, 2, buf);

	if (ret < 0)
		pr_err("%s: Error(%d)\n", __func__, ret);

	return ret;
}

static void max17042_write_reg_array(struct i2c_client *client,
				     u8 *buf, int size)
{
	int i;

	for (i = 0; i < size; i += 3)
		max17042_write_reg(client, (u8) (*(buf + i)), (buf + i) + 1);
}

static void max17042_init_regs(struct i2c_client *client)
{
	u8 data[2];

	if (max17042_read_reg(client, MAX17042_REG_FILTERCFG, data) < 0)
		return;

	/* Clear average vcell (12 sec) */
	data[0] &= 0x8f;

	max17042_write_reg(client, MAX17042_REG_FILTERCFG, data);
}

static void max17042_get_version(struct i2c_client *client)
{
	u8 data[2];

	if (max17042_read_reg(client, MAX17042_REG_VERSION, data) < 0)
		return;

	pr_debug("MAX17042 Fuel-Gauge Ver %d%d\n", data[0], data[1]);
}

static void max17042_alert_init(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data[2];

	/* SALRT Threshold setting */
	data[0] = fuelgauge->pdata->fuel_alert_soc;
	data[1] = 0xff;
	max17042_write_reg(client, MAX17042_REG_SALRT_TH, data);

	/* VALRT Threshold setting */
	data[0] = 0x00;
	data[1] = 0xff;
	max17042_write_reg(client, MAX17042_REG_VALRT_TH, data);

	/* TALRT Threshold setting */
	data[0] = 0x80;
	data[1] = 0x7f;
	max17042_write_reg(client, MAX17042_REG_TALRT_TH, data);
}

static bool max17042_check_status(struct i2c_client *client)
{
	u8 data[2];
	bool ret = false;

	/* check if Smn was generated */
	if (max17042_read_reg(client, MAX17042_REG_STATUS, data) < 0)
		return ret;

	pr_info("%s : status_reg(%02x%02x)\n", __func__, data[1], data[0]);

	/* minimum SOC threshold exceeded. */
	if (data[1] & (0x1 << 2))
		ret = true;

	/* clear status reg */
	if (!ret) {
		data[1] = 0;
		max17042_write_reg(client, MAX17042_REG_STATUS, data);
		msleep(200);
	}

	return ret;
}

static int max17042_set_temperature(struct i2c_client *client, int temperature)
{
	u8 data[2];

	data[0] = 0;
	data[1] = temperature;
	max17042_write_reg(client, MAX17042_REG_TEMPERATURE, data);

	pr_debug("%s: temperature to (%d)\n", __func__, temperature);

	return temperature;
}

static int max17042_get_temperature(struct i2c_client *client)
{
	u8 data[2];
	s32 temperature = 0;

	if (max17042_read_reg(client, MAX17042_REG_TEMPERATURE, data) < 0)
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

	pr_debug("%s: temperature (%d)\n", __func__, temperature);

	return temperature;
}

/* soc should be 0.1% unit */
static int max17042_get_soc(struct i2c_client *client)
{
	u8 data[2];
	int soc;

	if (max17042_read_reg(client, MAX17042_REG_SOC_VF, data) < 0)
		return -EINVAL;

	soc = ((data[1] * 100) + (data[0] * 100 / 256)) / 10;

	pr_debug("%s : raw capacity (%d)\n", __func__, soc);

	return min(soc, 1000);
}

static int max17042_get_vfocv(struct i2c_client *client)
{
	u8 data[2];
	u32 vfocv = 0;

	if (max17042_read_reg(client, MAX17042_REG_VFOCV, data) < 0)
		return -EINVAL;

	vfocv = ((data[0] >> 3) + (data[1] << 5)) * 625 / 1000;

	pr_debug("%s : vfocv (%d)\n", __func__, vfocv);

	return vfocv;
}

static int max17042_get_vcell(struct i2c_client *client)
{
	u8 data[2];
	u32 vcell = 0;

	if (max17042_read_reg(client, MAX17042_REG_VCELL, data) < 0)
		return -EINVAL;

	vcell = ((data[0] >> 3) + (data[1] << 5)) * 625 / 1000;

	pr_debug("%s : vcell (%d)\n", __func__, vcell);

	return vcell;
}

static int max17042_get_avgvcell(struct i2c_client *client)
{
	u8 data[2];
	u32 avgvcell = 0;

	if (max17042_read_reg(client, MAX17042_REG_AVGVCELL, data) < 0)
		return -EINVAL;

	avgvcell = ((data[0] >> 3) + (data[1] << 5)) * 625 / 1000;

	pr_debug("%s : avgvcell (%d)\n", __func__, avgvcell);

	return avgvcell;
}

static int max17042_regs[] = {
	MAX17042_REG_STATUS,	/* 0x00 */
	MAX17042_REG_VALRT_TH,	/* 0x01 */
	MAX17042_REG_TALRT_TH,	/* 0x02 */
	MAX17042_REG_SALRT_TH,	/* 0x03 */
	MAX17042_REG_TEMPERATURE,	/* 0x08 */
	MAX17042_REG_VCELL,	/* 0x09 */
	MAX17042_REG_AVGVCELL,	/* 0x19 */
	MAX17042_REG_CONFIG,	/* 0x1D */
	MAX17042_REG_VERSION,	/* 0x21 */
	MAX17042_REG_LEARNCFG,	/* 0x28 */
	MAX17042_REG_FILTERCFG,	/* 0x29 */
	MAX17042_REG_MISCCFG,	/* 0x2B */
	MAX17042_REG_CGAIN,	/* 0x2E */
	MAX17042_REG_RCOMP,	/* 0x38 */
	MAX17042_REG_VFOCV,	/* 0xFB */
	MAX17042_REG_SOC_VF,	/* 0xFF */
	-1, /* end */
};

static void max17042_read_regs(struct i2c_client *client, char *str)
{
	int i;
	u8 data = 0;

	for (i = 0; ; i++) {
		if (max17042_regs[i] == -1)
			break;

		max17042_read_reg(client, max17042_regs[i], &data);
		sprintf(str+strlen(str), "%04xh, ", data);
	}
}

bool sec_hal_fg_init(struct i2c_client *client)
{
	/* initialize fuel gauge registers */
	max17042_init_regs(client);

	max17042_get_version(client);

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

	/* 1. Set max17042 alert configuration. */
	max17042_alert_init(client);

	if (max17042_read_reg(client, MAX17042_REG_CONFIG, data)
	    < 0)
		return -1;

	/*Enable Alert (Aen = 1) */
	data[0] |= (0x1 << 2);

	max17042_write_reg(client, MAX17042_REG_CONFIG, data);

	pr_debug("%s : config_reg(%02x%02x) irq(%d)\n",
		 __func__, data[1], data[0], fuelgauge->pdata->fg_irq);

	return true;
}

bool sec_hal_fg_is_fuelalerted(struct i2c_client *client)
{
	return max17042_check_status(client);
}

bool sec_hal_fg_fuelalert_process(void *irq_data, bool is_fuel_alerted)
{
	struct sec_fuelgauge_info *fuelgauge = irq_data;
	u8 data[2];

	/* update SOC */
	/* max17042_get_soc(fuelgauge->client); */

	if (is_fuel_alerted) {
		if (max17042_read_reg(fuelgauge->client,
				      MAX17042_REG_CONFIG, data) < 0)
			return false;

		data[1] |= (0x1 << 3);

		max17042_write_reg(fuelgauge->client,
				   MAX17042_REG_CONFIG, data);

		pr_info("%s: Fuel-alert Alerted!! (%02x%02x)\n",
			__func__, data[1], data[0]);
	} else {
		if (max17042_read_reg(fuelgauge->client,
				      MAX17042_REG_CONFIG, data)
		    < 0)
			return false;

		data[1] &= (~(0x1 << 3));

		max17042_write_reg(fuelgauge->client,
				   MAX17042_REG_CONFIG, data);

		pr_info("%s: Fuel-alert Released!! (%02x%02x)\n",
			__func__, data[1], data[0]);
	}

	max17042_read_reg(fuelgauge->client, MAX17042_REG_VCELL, data);
	pr_debug("%s : MAX17042_REG_VCELL(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_TEMPERATURE, data);
	pr_debug("%s : MAX17042_REG_TEMPERATURE(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_CONFIG, data);
	pr_debug("%s : MAX17042_REG_CONFIG(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_VFOCV, data);
	pr_debug("%s : MAX17042_REG_VFOCV(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_SOC_VF, data);
	pr_debug("%s : MAX17042_REG_SOC_VF(%02x%02x)\n",
		 __func__, data[1], data[0]);

	pr_debug("%s : FUEL GAUGE IRQ (%d)\n",
		 __func__, gpio_get_value(fuelgauge->pdata->fg_irq));

#if 0
	max17042_read_reg(fuelgauge->client, MAX17042_REG_STATUS, data);
	pr_debug("%s : MAX17042_REG_STATUS(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_VALRT_TH, data);
	pr_debug("%s : MAX17042_REG_VALRT_TH(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_TALRT_TH, data);
	pr_debug("%s : MAX17042_REG_TALRT_TH(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_SALRT_TH, data);
	pr_debug("%s : MAX17042_REG_SALRT_TH(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_AVGVCELL, data);
	pr_debug("%s : MAX17042_REG_AVGVCELL(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_VERSION, data);
	pr_debug("%s : MAX17042_REG_VERSION(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_LEARNCFG, data);
	pr_debug("%s : MAX17042_REG_LEARNCFG(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_MISCCFG, data);
	pr_debug("%s : MAX17042_REG_MISCCFG(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_CGAIN, data);
	pr_debug("%s : MAX17042_REG_CGAIN(%02x%02x)\n",
		 __func__, data[1], data[0]);

	max17042_read_reg(fuelgauge->client, MAX17042_REG_RCOMP, data);
	pr_debug("%s : MAX17042_REG_RCOMP(%02x%02x)\n",
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
		val->intval = max17042_get_vcell(client);
		break;
		/* Additional Voltage Information (mV) */
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		switch (val->intval) {
		case SEC_BATTEY_VOLTAGE_AVERAGE:
			val->intval = max17042_get_avgvcell(client);
			break;
		case SEC_BATTEY_VOLTAGE_OCV:
			val->intval = max17042_get_vfocv(client);
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
		val->intval = max17042_get_soc(client);
		break;
		/* Battery Temperature */
	case POWER_SUPPLY_PROP_TEMP:
		/* Target Temperature */
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		val->intval = max17042_get_temperature(client);
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
	case POWER_SUPPLY_PROP_ONLINE:
		break;
		/* Battery Temperature */
	case POWER_SUPPLY_PROP_TEMP:
		/* Target Temperature */
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		max17042_set_temperature(client, val->intval);
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

		max17042_read_regs(fg->client, str);
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
	u8 data[2];

	switch (offset) {
	case FG_REG:
		if (sscanf(buf, "%x\n", &x) == 1) {
			fg->reg_addr = x;
			max17042_read_reg(fg->client,
				fg->reg_addr, fg->reg_data);
			pr_debug("%s: (read) addr = 0x%x, data = 0x%02x%02x\n",
				 __func__, fg->reg_addr,
				 fg->reg_data[1], fg->reg_data[0]);
		}
		ret = count;
		break;
	case FG_DATA:
		if (sscanf(buf, "%x\n", &x) == 1) {
			data[0] = (x & 0xFF00) >> 8;
			data[1] = (x & 0x00FF);
			pr_debug("%s: (write) addr = 0x%x, data = 0x%02x%02x\n",
				__func__, fg->reg_addr,
				data[1], data[0]);
			max17042_write_reg(fg->client,
				fg->reg_addr, data);
		}
		ret = count;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
#endif

#ifdef CONFIG_FUELGAUGE_MAX17042_COULOMB_COUNTING
static int fg_get_battery_type(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);

	return fuelgauge->info.battery_type;
}

static int fg_i2c_read(struct i2c_client *client,
				u8 reg, u8 *data, u8 length)
{
	s32 value;

	value = i2c_smbus_read_i2c_block_data(client, reg, length, data);
	if (value < 0 || value != length) {
		pr_err("%s: Failed to fg_i2c_read. status = %d\n",
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
		pr_err("%s: Failed to fg_i2c_write, error code=%d\n",
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
		pr_err("%s: Failed to read addr(0x%x)\n", __func__, addr);
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
		pr_err("%s: Failed to write addr(0x%x)\n", __func__, addr);
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
		pr_err("%s: Failed to read addr(0x%x)\n", __func__, addr);
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
			pr_err("%s: verification failed (addr : 0x%x, w_data : 0x%x, r_data : 0x%x)\n",
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
		pr_err("%s: Failed to read VCELL\n", __func__);
		return;
	}

	w_data = (data[1]<<8) | data[0];

	temp = (w_data & 0xFFF) * 78125;
	average_vcell = temp / 1000000;

	temp = ((w_data & 0xF000) >> 4) * 78125;
	temp2 = temp / 1000000;
	average_vcell += (temp2 << 4);

	pr_info("%s : AVG_VCELL(%d), data(0x%04x)\n", __func__,
		average_vcell, (data[1]<<8) | data[0]);

	reg_data = fg_read_register(client, FULLCAP_REG);
	pr_info("%s : FULLCAP(%d), data(0x%04x)\n", __func__,
		reg_data/2, reg_data);

	reg_data = fg_read_register(client, REMCAP_REP_REG);
	pr_info("%s : REMCAP_REP(%d), data(0x%04x)\n", __func__,
		reg_data/2, reg_data);

	reg_data = fg_read_register(client, REMCAP_MIX_REG);
	pr_info("%s : REMCAP_MIX(%d), data(0x%04x)\n", __func__,
		reg_data/2, reg_data);

	reg_data = fg_read_register(client, REMCAP_AV_REG);
	pr_info("%s : REMCAP_AV(%d), data(0x%04x)\n", __func__,
		reg_data/2, reg_data);

}

static void fg_periodic_read(struct i2c_client *client)
{
	u8 reg;
	int i;
	int data[0x10];

	for (i = 0; i < 16; i++) {
		for (reg = 0; reg < 0x10; reg++)
			data[reg] = fg_read_register(client, reg + i * 0x10);

		pr_debug("%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,",
			data[0x00], data[0x01], data[0x02], data[0x03],
			data[0x04], data[0x05], data[0x06], data[0x07]);
		pr_debug("%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,",
			data[0x08], data[0x09], data[0x0a], data[0x0b],
			data[0x0c], data[0x0d], data[0x0e], data[0x0f]);
		if (i == 4)
			i = 13;
	}
	pr_debug("\n");
}

static void fg_read_regs(struct i2c_client *client, char *str)
{
	u8 reg;
	int i;
	int data[0x10];

	for (i = 0; i < 16; i++) {
		for (reg = 0; reg < 0x10; reg++)
			data[reg] = fg_read_register(client, reg + i * 0x10);

		sprintf(str, "%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,",
			data[0x00], data[0x01], data[0x02], data[0x03],
			data[0x04], data[0x05], data[0x06], data[0x07]);
		sprintf(str, "%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,%04xh,",
			data[0x08], data[0x09], data[0x0a], data[0x0b],
			data[0x0c], data[0x0d], data[0x0e], data[0x0f]);
		if (i == 4)
			i = 13;
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
		pr_err("%s: Failed to read VCELL\n", __func__);
		return -1;
	}

	w_data = (data[1]<<8) | data[0];

	temp = (w_data & 0xFFF) * 78125;
	vcell = temp / 1000000;

	temp = ((w_data & 0xF000) >> 4) * 78125;
	temp2 = temp / 1000000;
	vcell += (temp2 << 4);

	if (!(fuelgauge->info.pr_cnt % PRINT_COUNT))
		pr_info("%s : VCELL(%d), data(0x%04x)\n",
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
		pr_err("%s: Failed to read VFOCV\n", __func__);
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
		pr_err("%s: Failed to read STATUS_REG\n", __func__);
		return 0;
	}

	if (status_data[0] & (0x1 << 3)) {
		pr_info("%s - addr(0x01), data(0x%04x)\n", __func__,
			(status_data[1]<<8) | status_data[0]);
		pr_info("%s : battery is absent!!\n", __func__);
		ret = 0;
	}

	return ret;
}


static int fg_read_temp(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data[2];
	int temper = 0;

	if (fg_check_battery_present(client)) {
		if (fg_i2c_read(client, TEMPERATURE_REG, data, 2) < 0) {
			pr_err("%s: Failed to read TEMPERATURE_REG\n",
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

			/* Adjust temperature over 47,
			 * only for SDI battery type
			 */
			if (fg_get_battery_type(client) == SDI_BATTERY_TYPE) {
				if (temper >= 47000 && temper < 60000)
					temper = (temper * SDI_TRIM1_1 / 100) -
						SDI_TRIM1_2;
				else if (temper >= 60000)
					temper = (temper * SDI_TRIM2_1 / 100) -
						51000;
			}
		}
	} else
		temper = 20000;

	if (!(fuelgauge->info.pr_cnt % PRINT_COUNT))
		pr_info("%s : TEMPERATURE(%d), data(0x%04x)\n", __func__,
		temper, (data[1]<<8) | data[0]);

	return temper/100;
}

static int fg_read_vfsoc(struct i2c_client *client)
{
	u8 data[2];
	int soc;

	if (fg_i2c_read(client, VFSOC_REG, data, 2) < 0) {
		pr_err("%s: Failed to read VFSOC\n", __func__);
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
		pr_err("%s: Failed to read SOCREP\n", __func__);
		return -1;
	}

	soc = ((data[1] * 100) + (data[0] * 100 / 256)) / 10;

	pr_debug("%s : raw capacity (%d)\n", __func__, soc);

	if (!(fuelgauge->info.pr_cnt % PRINT_COUNT))
		pr_debug("%s : raw capacity (%d), data(0x%04x)\n",
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
		pr_err("%s: Failed to read CURRENT\n", __func__);
		return -1;
	}

	if (fg_i2c_read(client, AVG_CURRENT_REG, data2, 2) < 0) {
		pr_err("%s: Failed to read AVERAGE CURRENT\n", __func__);
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
		pr_info("%s : CURRENT(%dmA), AVG_CURRENT(%dmA)\n", __func__,
			i_current, avg_current);
		fuelgauge->info.pr_cnt = 1;
		/* Read max17042's all registers every 5 minute. */
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
		pr_err("%s: Failed to read AVERAGE CURRENT\n", __func__);
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

	/* delay for current stablization */
	msleep(500);

	pr_info("%s : Before quick-start - VCELL(%d), VFOCV(%d), VfSOC(%d), RepSOC(%d)\n",
		__func__, fg_read_vcell(client), fg_read_vfocv(client),
		fg_read_vfsoc(client), fg_read_soc(client));
	pr_info("%s : Before quick-start - current(%d), avg current(%d)\n",
		__func__, fg_read_current(client),
		fg_read_avg_current(client));

	if (!fuelgauge->pdata->check_jig_status()) {
		pr_info("%s : Return by No JIG_ON signal\n", __func__);
		return 0;
	}

	fg_write_register(client, CYCLES_REG, 0);

	if (fg_i2c_read(client, MISCCFG_REG, data, 2) < 0) {
		pr_err("%s: Failed to read MiscCFG\n", __func__);
		return -1;
	}

	data[1] |= (0x1 << 2);
	if (fg_i2c_write(client, MISCCFG_REG, data, 2) < 0) {
		pr_err("%s: Failed to write MiscCFG\n", __func__);
		return -1;
	}

	msleep(250);
	fg_write_register(client, FULLCAP_REG, fuelgauge->info.capacity);
	msleep(500);

	pr_info("%s : After quick-start - VCELL(%d), VFOCV(%d), VfSOC(%d), RepSOC(%d)\n",
		__func__, fg_read_vcell(client), fg_read_vfocv(client),
		fg_read_vfsoc(client), fg_read_soc(client));
	pr_info("%s : After quick-start - current(%d), avg current(%d)\n",
		__func__, fg_read_current(client),
		fg_read_avg_current(client));
	fg_write_register(client, CYCLES_REG, 0x00a0);

/* P8 is not turned off by Quickstart @3.4V
 * (It's not a problem, depend on mode data)
 * Power off for factory test(File system, etc..) */
#if defined(CONFIG_MACH_P8_REV00) || defined(CONFIG_MACH_P8_REV01) || \
	defined(CONFIG_MACH_P8LTE_REV00)
#define QUICKSTART_POWER_OFF_VOLTAGE	3400
	vfocv = fg_read_vfocv(client);
	if (vfocv < QUICKSTART_POWER_OFF_VOLTAGE) {
		pr_info("%s: Power off condition(%d)\n", __func__, vfocv);

		fullcap = fg_read_register(client, FULLCAP_REG);
		/* FullCAP * 0.009 */
		fg_write_register(client, REMCAP_REP_REG,
			(u16)(fullcap * 9 / 1000));
		msleep(200);
		pr_info("%s : new soc=%d, vfocv=%d\n", __func__,
			fg_read_soc(client), vfocv);
	}

	pr_info("%s : Additional step - VfOCV(%d), VfSOC(%d), RepSOC(%d)\n",
		__func__, fg_read_vfocv(client),
		fg_read_vfsoc(client), fg_read_soc(client));
#endif

	return 0;
}


int fg_reset_capacity(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);

	return fg_write_register(client, DESIGNCAP_REG,
		fuelgauge->info.vfcapacity-1);
}

int fg_adjust_capacity(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 data[2];

	data[0] = 0;
	data[1] = 0;

	/* 1. Write RemCapREP(05h)=0; */
	if (fg_i2c_write(client, REMCAP_REP_REG, data, 2) < 0) {
		pr_err("%s: Failed to write RemCap_REP\n", __func__);
		return -1;
	}
	msleep(200);

	pr_info("%s : After adjust - RepSOC(%d)\n", __func__,
		fg_read_soc(client));

	fuelgauge->info.soc_restart_flag = 1;

	return 0;
}

void fg_low_batt_compensation(struct i2c_client *client, u32 level)
{
	int read_val;
	u32 temp;

	pr_info("%s : Adjust SOCrep to %d!!\n", __func__, level);

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

	/* fuelgauge->info.low_batt_comp_flag = 1; */
}

static void fg_read_model_data(struct i2c_client *client)
{
	u16 data0[16], data1[16], data2[16];
	int i;
	int relock_check;

	pr_info("[FG_Model] ");

	/* Unlock model access */
	fg_write_register(client, 0x62, 0x0059);
	fg_write_register(client, 0x63, 0x00C4);

	/* Read model data */
	fg_read_16register(client, 0x80, data0);
	fg_read_16register(client, 0x90, data1);
	fg_read_16register(client, 0xa0, data2);

	/* Print model data */
	for (i = 0; i < 16; i++)
		pr_info("0x%04x, ", data0[i]);

	for (i = 0; i < 16; i++)
		pr_info("0x%04x, ", data1[i]);

	for (i = 0; i < 16; i++) {
		if (i == 15)
			pr_info("0x%04x", data2[i]);
		else
			pr_info("0x%04x, ", data2[i]);
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
				pr_info("%s : data is non-zero, lock again!!\n",
					__func__);
				relock_check = 1;
			}
		}
	} while (relock_check);

}

int fg_alert_init(struct i2c_client *client)
{
	u8 misccgf_data[2];
	u8 salrt_data[2];
	u8 config_data[2];
	u8 valrt_data[2];
	u8 talrt_data[2];
	u16 read_data = 0;

	/* Using RepSOC */
	if (fg_i2c_read(client, MISCCFG_REG, misccgf_data, 2) < 0) {
		pr_err("%s: Failed to read MISCCFG_REG\n", __func__);
		return -1;
	}
	misccgf_data[0] = misccgf_data[0] & ~(0x03);

	if (fg_i2c_write(client, MISCCFG_REG, misccgf_data, 2) < 0) {
		pr_info("%s: Failed to write MISCCFG_REG\n", __func__);
		return -1;
	}

	/* SALRT Threshold setting */
	salrt_data[1] = 0xff;
	salrt_data[0] = 0x01;
	if (fg_i2c_write(client, SALRT_THRESHOLD_REG, salrt_data, 2) < 0) {
		pr_info("%s: Failed to write SALRT_THRESHOLD_REG\n", __func__);
		return -1;
	}

	/* Reset VALRT Threshold setting (disable) */
	valrt_data[1] = 0xFF;
	valrt_data[0] = 0x00;
	if (fg_i2c_write(client, VALRT_THRESHOLD_REG, valrt_data, 2) < 0) {
		pr_info("%s: Failed to write VALRT_THRESHOLD_REG\n", __func__);
		return -1;
	}

	read_data = fg_read_register(client, (u8)VALRT_THRESHOLD_REG);
	if (read_data != 0xff00)
		pr_err("%s : VALRT_THRESHOLD_REG is not valid (0x%x)\n",
			__func__, read_data);

	/* Reset TALRT Threshold setting (disable) */
	talrt_data[1] = 0x7F;
	talrt_data[0] = 0x80;
	if (fg_i2c_write(client, TALRT_THRESHOLD_REG, talrt_data, 2) < 0) {
		pr_info("%s: Failed to write TALRT_THRESHOLD_REG\n", __func__);
		return -1;
	}

	read_data = fg_read_register(client, (u8)TALRT_THRESHOLD_REG);
	if (read_data != 0x7f80)
		pr_err("%s : TALRT_THRESHOLD_REG is not valid (0x%x)\n",
			__func__, read_data);

	mdelay(100);

	/* Enable SOC alerts */
	if (fg_i2c_read(client, CONFIG_REG, config_data, 2) < 0) {
		pr_err("%s: Failed to read CONFIG_REG\n", __func__);
		return -1;
	}
	config_data[0] = config_data[0] | (0x1 << 2);

	if (fg_i2c_write(client, CONFIG_REG, config_data, 2) < 0) {
		pr_info("%s: Failed to write CONFIG_REG\n", __func__);
		return -1;
	}

	return 1;
}

static int fg_check_status_reg(struct i2c_client *client)
{
	u8 status_data[2];
	int ret = 0;

	/* 1. Check Smn was generatedread */
	if (fg_i2c_read(client, STATUS_REG, status_data, 2) < 0) {
		pr_err("%s: Failed to read STATUS_REG\n", __func__);
		return -1;
	}
	pr_info("%s - addr(0x00), data(0x%04x)\n", __func__,
		(status_data[1]<<8) | status_data[0]);

	if (status_data[1] & (0x1 << 2))
		ret = 1;

	/* 2. clear Status reg */
	status_data[1] = 0;
	if (fg_i2c_write(client, STATUS_REG, status_data, 2) < 0) {
		pr_info("%s: Failed to write STATUS_REG\n", __func__);
		return -1;
	}

	return ret;
}

void fg_fullcharged_compensation(struct i2c_client *client,
		u32 is_recharging, u32 pre_update)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	static int new_fullcap_data;

	pr_info("%s : is_recharging(%d), pre_update(%d)\n",
		__func__, is_recharging, pre_update);

	new_fullcap_data =
		fg_read_register(client, FULLCAP_REG);
	if (new_fullcap_data < 0)
		new_fullcap_data = fuelgauge->info.capacity;

	if (new_fullcap_data >
		(fuelgauge->info.capacity * 110 / 100)) {
		pr_info("%s : [Case 1] previous_fullcap = 0x%04x, NewFullCap = 0x%04x\n",
			__func__, fuelgauge->info.previous_fullcap,
			new_fullcap_data);

		new_fullcap_data =
			(fuelgauge->info.capacity * 110) / 100;
		fg_write_register(client, REMCAP_REP_REG,
			(u16)(new_fullcap_data));
		fg_write_register(client, FULLCAP_REG,
			(u16)(new_fullcap_data));
	} else if (new_fullcap_data <
		(fuelgauge->info.capacity * 70 / 100)) {
		pr_info("%s : [Case 5] previous_fullcap = 0x%04x, NewFullCap = 0x%04x\n",
			__func__, fuelgauge->info.previous_fullcap,
			new_fullcap_data);

		new_fullcap_data =
			(fuelgauge->info.capacity * 70) / 100;
		fg_write_register(client, REMCAP_REP_REG,
			(u16)(new_fullcap_data));
		fg_write_register(client, FULLCAP_REG,
			(u16)(new_fullcap_data));
	} else {
		if (new_fullcap_data >
			(fuelgauge->info.previous_fullcap * 105 / 100)) {
			pr_info("%s : [Case 2] previous_fullcap = 0x%04x, NewFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_fullcap,
				new_fullcap_data);

			new_fullcap_data =
				(fuelgauge->info.previous_fullcap * 105) / 100;
			fg_write_register(client, REMCAP_REP_REG,
				(u16)(new_fullcap_data));
			fg_write_register(client, FULLCAP_REG,
				(u16)(new_fullcap_data));
		} else if (new_fullcap_data <
			(fuelgauge->info.previous_fullcap * 90 / 100)) {
			pr_info("%s : [Case 3] previous_fullcap = 0x%04x, NewFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_fullcap,
				new_fullcap_data);

			new_fullcap_data =
				(fuelgauge->info.previous_fullcap * 90) / 100;
			fg_write_register(client, REMCAP_REP_REG,
				(u16)(new_fullcap_data));
			fg_write_register(client, FULLCAP_REG,
				(u16)(new_fullcap_data));
		} else {
			pr_info("%s : [Case 4] previous_fullcap = 0x%04x, NewFullCap = 0x%04x\n",
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

	pr_info("%s : (A) FullCap = 0x%04x, RemCap = 0x%04x\n", __func__,
		fg_read_register(client, FULLCAP_REG),
		fg_read_register(client, REMCAP_REP_REG));

	fg_periodic_read(client);

}

void fg_check_vf_fullcap_range(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	static int new_vffullcap;
	u16 print_flag = 1;

	new_vffullcap = fg_read_register(client, FULLCAP_NOM_REG);
	if (new_vffullcap < 0)
		new_vffullcap = fuelgauge->info.vfcapacity;

	if (new_vffullcap >
		(fuelgauge->info.vfcapacity * 110 / 100)) {
		pr_info("%s : [Case 1] previous_vffullcap = 0x%04x, NewVfFullCap = 0x%04x\n",
			__func__, fuelgauge->info.previous_vffullcap,
			new_vffullcap);

		new_vffullcap =
			(fuelgauge->info.vfcapacity * 110) / 100;

		fg_write_register(client, DQACC_REG,
			(u16)(new_vffullcap / 4));
		fg_write_register(client, DPACC_REG, (u16)0x3200);
	} else if (new_vffullcap <
		(fuelgauge->info.vfcapacity * 70 / 100)) {
		pr_info("%s : [Case 5] previous_vffullcap = 0x%04x, NewVfFullCap = 0x%04x\n",
			__func__, fuelgauge->info.previous_vffullcap,
			new_vffullcap);

		new_vffullcap =
			(fuelgauge->info.vfcapacity * 70) / 100;

		fg_write_register(client, DQACC_REG,
			(u16)(new_vffullcap / 4));
		fg_write_register(client, DPACC_REG, (u16)0x3200);
	} else {
		if (new_vffullcap >
			(fuelgauge->info.previous_vffullcap * 105 / 100)) {
			pr_info("%s : [Case 2] previous_vffullcap = 0x%04x, NewVfFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_vffullcap,
				new_vffullcap);

			new_vffullcap =
				(fuelgauge->info.previous_vffullcap * 105) /
				100;

			fg_write_register(client, DQACC_REG,
				(u16)(new_vffullcap / 4));
			fg_write_register(client, DPACC_REG, (u16)0x3200);
		} else if (new_vffullcap <
			(fuelgauge->info.previous_vffullcap * 90 / 100)) {
			pr_info("%s : [Case 3] previous_vffullcap = 0x%04x, NewVfFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_vffullcap,
				new_vffullcap);

			new_vffullcap =
				(fuelgauge->info.previous_vffullcap * 90) / 100;

			fg_write_register(client, DQACC_REG,
				(u16)(new_vffullcap / 4));
			fg_write_register(client, DPACC_REG, (u16)0x3200);
		} else {
			pr_info("%s : [Case 4] previous_vffullcap = 0x%04x, NewVfFullCap = 0x%04x\n",
				__func__, fuelgauge->info.previous_vffullcap,
				new_vffullcap);
			print_flag = 0;
		}
	}

	/* delay for register setting (dQacc, dPacc) */
	if (print_flag)
		msleep(300);

	fuelgauge->info.previous_vffullcap =
		fg_read_register(client, FULLCAP_NOM_REG);

	if (print_flag)
		pr_info("%s : VfFullCap(0x%04x), dQacc(0x%04x), dPacc(0x%04x)\n",
			__func__,
			fg_read_register(client, FULLCAP_NOM_REG),
			fg_read_register(client, DQACC_REG),
			fg_read_register(client, DPACC_REG));

}

int fg_check_cap_corruption(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	int vfsoc = fg_read_vfsoc(client);
	int repsoc = fg_read_soc(client);
	int mixcap = fg_read_register(client, REMCAP_MIX_REG);
	int vfocv = fg_read_register(client, VFOCV_REG);
	int remcap = fg_read_register(client, REMCAP_REP_REG);
	int fullcapacity = fg_read_register(client, FULLCAP_REG);
	int vffullcapacity = fg_read_register(client, FULLCAP_NOM_REG);
	u32 temp, temp2, new_vfocv, pr_vfocv;
	int ret = 0;

	/* If usgin Jig or low batt compensation flag is set,
	 * then skip checking.
	 */
	if (fuelgauge->pdata->check_jig_status()) {
		pr_info("%s : Return by Using Jig(%d)\n", __func__,
			fuelgauge->pdata->check_jig_status());
		return 0;
	}

	if (vfsoc < 0 || repsoc < 0 || mixcap < 0 || vfocv < 0 ||
		remcap < 0 || fullcapacity < 0 || vffullcapacity < 0)
		return 0;

	/* Check full charge learning case. */
	if (((vfsoc >= 90) && ((remcap >= (fullcapacity * 995 / 1000)) &&
		(remcap <= (fullcapacity * 1005 / 1000)))) ||
		fuelgauge->info.low_batt_comp_flag ||
		fuelgauge->info.soc_restart_flag) {
		pr_info("%s : RemCap(%d), FullCap(%d), SOC(%d), ",
					__func__, (remcap/2),
					(fullcapacity/2), repsoc);
		pr_info("low_batt_comp_flag(%d), soc_restart_flag(%d)\n",
					fuelgauge->info.low_batt_comp_flag,
					fuelgauge->info.soc_restart_flag);
		fuelgauge->info.previous_repsoc = repsoc;
		fuelgauge->info.previous_remcap = remcap;
		fuelgauge->info.previous_fullcapacity = fullcapacity;
		if (fuelgauge->info.soc_restart_flag)
			fuelgauge->info.soc_restart_flag = 0;

		ret = 1;
	}

	/* ocv calculation for print */
	temp = (vfocv & 0xFFF) * 78125;
	pr_vfocv = temp / 1000000;

	temp = ((vfocv & 0xF000) >> 4) * 78125;
	temp2 = temp / 1000000;
	pr_vfocv += (temp2 << 4);

	/* MixCap differ is greater than 265mAh */
	if ((((vfsoc+5) < fuelgauge->info.previous_vfsoc) ||
		(vfsoc > (fuelgauge->info.previous_vfsoc+5))) ||
		(((repsoc+5) < fuelgauge->info.previous_repsoc) ||
		(repsoc > (fuelgauge->info.previous_repsoc+5))) ||
		(((mixcap+530) < fuelgauge->info.previous_mixcap) ||
		(mixcap > (fuelgauge->info.previous_mixcap+530)))) {
		fg_periodic_read(client);

		pr_info("[FG_Recovery] (B) VfSOC(%d), prevVfSOC(%d), RepSOC(%d), prevRepSOC(%d), MixCap(%d), prevMixCap(%d),VfOCV(0x%04x, %d)\n",
			vfsoc,
			fuelgauge->info.previous_vfsoc,
			repsoc, fuelgauge->info.previous_repsoc,
			(mixcap/2),
			(fuelgauge->info.previous_mixcap/2),
			vfocv,
			pr_vfocv);

		mutex_lock(&fuelgauge->fg_lock);

		fg_write_and_verify_register(client, REMCAP_MIX_REG,
			fuelgauge->info.previous_mixcap);
		fg_write_register(client, VFOCV_REG,
			fuelgauge->info.previous_vfocv);
		mdelay(200);

		fg_write_and_verify_register(client, REMCAP_REP_REG,
			fuelgauge->info.previous_remcap);
		vfsoc = fg_read_register(client, VFSOC_REG);
		fg_write_register(client, 0x60, 0x0080);
		fg_write_and_verify_register(client, 0x48, vfsoc);
		fg_write_register(client, 0x60, 0x0000);

		fg_write_and_verify_register(client, 0x45,
			(fuelgauge->info.previous_vfcapacity / 4));
		fg_write_and_verify_register(client, 0x46, 0x3200);
		fg_write_and_verify_register(client, FULLCAP_REG,
			fuelgauge->info.previous_fullcapacity);
		fg_write_and_verify_register(client, FULLCAP_NOM_REG,
			fuelgauge->info.previous_vfcapacity);

		mutex_unlock(&fuelgauge->fg_lock);

		msleep(200);

		/* ocv calculation for print */
		new_vfocv = fg_read_register(client, VFOCV_REG);
		temp = (new_vfocv & 0xFFF) * 78125;
		pr_vfocv = temp / 1000000;

		temp = ((new_vfocv & 0xF000) >> 4) * 78125;
		temp2 = temp / 1000000;
		pr_vfocv += (temp2 << 4);

		pr_info("[FG_Recovery] (A) newVfSOC(%d), newRepSOC(%d), newMixCap(%d), newVfOCV(0x%04x, %d)\n",
			fg_read_vfsoc(client),
			fg_read_soc(client),
			(fg_read_register(client, REMCAP_MIX_REG)/2),
			new_vfocv,
			pr_vfocv);

		fg_periodic_read(client);

		ret = 1;
	} else {
		fuelgauge->info.previous_vfsoc = vfsoc;
		fuelgauge->info.previous_repsoc = repsoc;
		fuelgauge->info.previous_remcap = remcap;
		fuelgauge->info.previous_mixcap = mixcap;
		fuelgauge->info.previous_fullcapacity = fullcapacity;
		fuelgauge->info.previous_vfcapacity = vffullcapacity;
		fuelgauge->info.previous_vfocv = vfocv;
	}

	return ret;
}

void fg_set_full_charged(struct i2c_client *client)
{
	pr_info("[FG_Set_Full] (B) FullCAP(%d), RemCAP(%d)\n",
		(fg_read_register(client, FULLCAP_REG)/2),
		(fg_read_register(client, REMCAP_REP_REG)/2));

	fg_write_register(client, FULLCAP_REG,
		(u16)fg_read_register(client, REMCAP_REP_REG));

	pr_info("[FG_Set_Full] (A) FullCAP(%d), RemCAP(%d)\n",
		(fg_read_register(client, FULLCAP_REG)/2),
		(fg_read_register(client, REMCAP_REP_REG)/2));
}

static void display_low_batt_comp_cnt(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	u8 type_str[10];

	if (fg_get_battery_type(client) == SDI_BATTERY_TYPE)
		sprintf(type_str, "SDI");
	else if (fg_get_battery_type(client) == ATL_BATTERY_TYPE)
		sprintf(type_str, "ATL");
	else
		sprintf(type_str, "Unknown");

	pr_info("Check Array(%s) : [%d, %d], [%d, %d], ", type_str,
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
	int repsoc, repsoc_data = 0;
	int read_val;
	u8 data[2];

	if (fg_i2c_read(client, SOCREP_REG, data, 2) < 0) {
		pr_err("%s: Failed to read SOCREP\n", __func__);
		return;
	}
	repsoc = data[1];
	repsoc_data = ((data[1] << 8) | data[0]);

	if (repsoc_data > POWER_OFF_SOC_HIGH_MARGIN)
		return;

	pr_info("%s : soc=%d%%(0x%04x), vcell=%d\n", __func__,
		repsoc, repsoc_data, vcell);
	if (vcell > POWER_OFF_VOLTAGE_HIGH_MARGIN) {
		read_val = fg_read_register(client, FULLCAP_REG);
		/* FullCAP * 0.013 */
		fg_write_register(client, REMCAP_REP_REG,
		(u16)(read_val * 13 / 1000));
		msleep(200);
		*fg_soc = fg_read_soc(client);
		pr_info("%s : new soc=%d, vcell=%d\n", __func__,
			*fg_soc, vcell);
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
				int range, int level, int nCurrent)
{
	int ret = 0;

	if (fg_get_battery_type(client) == SDI_BATTERY_TYPE) {
		switch (range) {
/* P4W & P8 needs one more level */
#if defined(CONFIG_MACH_P4W_REV00) || defined(CONFIG_MACH_P4W_REV01) ||	\
	defined(CONFIG_MACH_P8_REV00) || defined(CONFIG_MACH_P8_REV01) || \
	defined(CONFIG_MACH_P8LTE_REV00)
		case 5:
			if (level == 1)
				ret = SDI_Range5_1_Offset +
					((nCurrent * SDI_Range5_1_Slope) /
					1000);
			else if (level == 3)
				ret = SDI_Range5_3_Offset +
					((nCurrent * SDI_Range5_3_Slope) /
					1000);
			break;
#endif
		case 4:
			if (level == 1)
				ret = SDI_Range4_1_Offset +
					((nCurrent * SDI_Range4_1_Slope) /
					1000);
			else if (level == 3)
				ret = SDI_Range4_3_Offset +
					((nCurrent * SDI_Range4_3_Slope) /
					1000);
			break;

		case 3:
			if (level == 1)
				ret = SDI_Range3_1_Offset +
					((nCurrent * SDI_Range3_1_Slope) /
					1000);
			else if (level == 3)
				ret = SDI_Range3_3_Offset +
					((nCurrent * SDI_Range3_3_Slope) /
					1000);
			break;

		case 2:
			if (level == 1)
				ret = SDI_Range2_1_Offset +
					((nCurrent * SDI_Range2_1_Slope) /
					1000);
			else if (level == 3)
				ret = SDI_Range2_3_Offset +
					((nCurrent * SDI_Range2_3_Slope) /
					1000);
			break;

		case 1:
			if (level == 1)
				ret = SDI_Range1_1_Offset +
					((nCurrent * SDI_Range1_1_Slope) /
					1000);
			else if (level == 3)
				ret = SDI_Range1_3_Offset +
					((nCurrent * SDI_Range1_3_Slope) /
					1000);
			break;

		default:
			break;
		}
	}  else if (fg_get_battery_type(client) ==
		ATL_BATTERY_TYPE) {
		switch (range) {
		case 4:
			if (level == 1)
				ret = ATL_Range4_1_Offset +
					((nCurrent * ATL_Range4_1_Slope) /
					1000);
			else if (level == 3)
				ret = ATL_Range4_3_Offset +
					((nCurrent * ATL_Range4_3_Slope) /
					1000);
			break;

		case 3:
			if (level == 1)
				ret = ATL_Range3_1_Offset +
					((nCurrent * ATL_Range3_1_Slope) /
					1000);
			else if (level == 3)
				ret = ATL_Range3_3_Offset +
					((nCurrent * ATL_Range3_3_Slope) /
					1000);
			break;

		case 2:
			if (level == 1)
				ret = ATL_Range2_1_Offset +
					((nCurrent * ATL_Range2_1_Slope) /
					1000);
			else if (level == 3)
				ret = ATL_Range2_3_Offset +
					((nCurrent * ATL_Range2_3_Slope) /
					1000);
			break;

		case 1:
			if (level == 1)
				ret = ATL_Range1_1_Offset +
					((nCurrent * ATL_Range1_1_Slope) /
					1000);
			else if (level == 3)
				ret = ATL_Range1_3_Offset +
					((nCurrent * ATL_Range1_3_Slope) /
					1000);
			break;

		default:
			break;
		}
	}

	return ret;
}

int low_batt_compensation(struct i2c_client *client,
		int fg_soc, int fg_vcell, int fg_current)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);
	int fg_avg_current = 0;
	int fg_min_current = 0;
	int new_level = 0;
	int bCntReset = 0;

	/* Not charging, flag is none, Under 3.60V or 3.45V */
	if (!fuelgauge->info.low_batt_comp_flag
		&& (fg_vcell <= fuelgauge->info.check_start_vol)) {
		fg_avg_current = fg_read_avg_current(client);
		fg_min_current = min(fg_avg_current, fg_current);

		if (fg_min_current < CURRENT_RANGE_MAX) {
			if (fg_soc >= 2 &&
				fg_vcell < get_low_batt_threshold(client,
				CURRENT_RANGE_MAX_NUM, 1, fg_min_current))
				add_low_batt_comp_cnt(client,
				CURRENT_RANGE_MAX_NUM, 1);
			else if (fg_soc >= 4 &&
				fg_vcell < get_low_batt_threshold(client,
				CURRENT_RANGE_MAX_NUM, 3, fg_min_current))
				add_low_batt_comp_cnt(client,
				CURRENT_RANGE_MAX_NUM, 3);
			else
				bCntReset = 1;
		}
/* P4W & P8 needs more level */
#if defined(CONFIG_MACH_P4W_REV00) || defined(CONFIG_MACH_P4W_REV01) || \
	defined(CONFIG_MACH_P8_REV00) || defined(CONFIG_MACH_P8_REV01) || \
	defined(CONFIG_MACH_P8LTE_REV00)
		else if (fg_min_current >= CURRENT_RANGE5 &&
			fg_min_current < CURRENT_RANGE4) {
			if (fg_soc >= 2 && fg_vcell <
				get_low_batt_threshold(client,
				4, 1, fg_min_current))
				add_low_batt_comp_cnt(client, 4, 1);
			else if (fg_soc >= 4 && fg_vcell <
				get_low_batt_threshold(client,
				4, 3, fg_min_current))
				add_low_batt_comp_cnt(client, 4, 3);
			else
				bCntReset = 1;
		}
#endif
		else if (fg_min_current >= CURRENT_RANGE4 &&
			fg_min_current < CURRENT_RANGE3) {
			if (fg_soc >= 2 && fg_vcell <
				get_low_batt_threshold(client,
				3, 1, fg_min_current))
				add_low_batt_comp_cnt(client, 3, 1);
			else if (fg_soc >= 4 && fg_vcell <
				get_low_batt_threshold(client,
				3, 3, fg_min_current))
				add_low_batt_comp_cnt(client, 3, 3);
			else
				bCntReset = 1;
		} else if (fg_min_current >= CURRENT_RANGE3 &&
		fg_min_current < CURRENT_RANGE2) {
			if (fg_soc >= 2 && fg_vcell <
				get_low_batt_threshold(client,
				2, 1, fg_min_current))
				add_low_batt_comp_cnt(client, 2, 1);
			else if (fg_soc >= 4 && fg_vcell <
				get_low_batt_threshold(client,
				2, 3, fg_min_current))
				add_low_batt_comp_cnt(client, 2, 3);
			else
				bCntReset = 1;
		} else if (fg_min_current >= CURRENT_RANGE2 &&
		fg_min_current < CURRENT_RANGE1) {
			if (fg_soc >= 2 && fg_vcell <
				get_low_batt_threshold(client,
				1, 1, fg_min_current))
				add_low_batt_comp_cnt(client, 1, 1);
			else if (fg_soc >= 4 && fg_vcell <
				get_low_batt_threshold(client,
				1, 3, fg_min_current))
				add_low_batt_comp_cnt(client, 1, 3);
			else
				bCntReset = 1;
		}


		if (check_low_batt_comp_condition(client, &new_level)) {
#if defined(CONFIG_MACH_P8_REV00) || \
		defined(CONFIG_MACH_P8_REV01) || \
		defined(CONFIG_MACH_P8LTE_REV00)
			/* Disable 3% low battery compensation (only for P8s) */
			/* duplicated action with 1% low battery compensation */
			if (new_level < 2)
#endif
				fg_low_batt_compensation(client, new_level);
			reset_low_batt_comp_cnt(client);
		}

		if (bCntReset)
			reset_low_batt_comp_cnt(client);

		/* if compensation finished, then read SOC again!!*/
		if (fuelgauge->info.low_batt_comp_flag) {
			pr_info("%s : MIN_CURRENT(%d), AVG_CURRENT(%d), CURRENT(%d), SOC(%d), VCELL(%d)\n",
				__func__, fg_min_current, fg_avg_current,
				fg_current, fg_soc, fg_vcell);
#if defined(CONFIG_MACH_P8_REV00) || \
	defined(CONFIG_MACH_P8_REV01) || \
	defined(CONFIG_MACH_P8LTE_REV00)
	/* Do not update soc right after low battery compensation */
	/* to prevent from powering-off suddenly (only for P8s) */
			pr_info("%s : SOC is set to %d\n",
				__func__, fg_read_soc(client));
#else
			fg_soc = fg_read_soc(client);
			pr_info("%s : SOC is set to %d\n", __func__, fg_soc);
#endif
		}
	}

#if defined(CONFIG_MACH_P4W_REV00) || defined(CONFIG_MACH_P4W_REV01) ||	\
	defined(CONFIG_MACH_P2_REV00) || defined(CONFIG_MACH_P2_REV01) || \
	defined(CONFIG_MACH_P2_REV02)
	/* Prevent power off over 3500mV */
	prevent_early_poweroff(fg_vcell, &fg_soc);
#endif

	return fg_soc;
}

static void fg_set_battery_type(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);

	u16 data;
	u8 type_str[10];

	data = fg_read_register(client, DESIGNCAP_REG);

	if ((data == sdi_vfcapacity) || (data == sdi_vfcapacity-1))
		fuelgauge->info.battery_type = SDI_BATTERY_TYPE;
	else if ((data == atl_vfcapacity) || (data == atl_vfcapacity-1))
		fuelgauge->info.battery_type = ATL_BATTERY_TYPE;
	else {
		pr_info("%s : Unknown battery is set to SDI type.\n", __func__);
		fuelgauge->info.battery_type = SDI_BATTERY_TYPE;
	}

	if (fuelgauge->info.battery_type == SDI_BATTERY_TYPE)
		sprintf(type_str, "SDI");
	else if (fuelgauge->info.battery_type == ATL_BATTERY_TYPE)
		sprintf(type_str, "ATL");
	else
		sprintf(type_str, "Unknown");

	pr_info("%s : DesignCAP(0x%04x), Battery type(%s)\n",
		__func__, data, type_str);

	switch (fuelgauge->info.battery_type) {
	case ATL_BATTERY_TYPE:
		fuelgauge->info.capacity = atl_capacity;
		fuelgauge->info.vfcapacity = atl_vfcapacity;
		break;

	case SDI_BATTERY_TYPE:
	default:
		fuelgauge->info.capacity = sdi_capacity;
		fuelgauge->info.vfcapacity = sdi_vfcapacity;
		break;
	}

	/* If not initialized yet, then init threshold values. */
	if (!fuelgauge->info.check_start_vol) {
		if (fuelgauge->info.battery_type == SDI_BATTERY_TYPE)
			fuelgauge->info.check_start_vol =
			sdi_low_bat_comp_start_vol;
		else if (fuelgauge->info.battery_type == ATL_BATTERY_TYPE)
			fuelgauge->info.check_start_vol =
			atl_low_bat_comp_start_vol;
	}

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

	case FG_BATTERY_TYPE:
		ret = fg_get_battery_type(client);
		break;

	case FG_CHECK_STATUS:
		ret = fg_check_status_reg(client);
		break;

	case FG_VF_SOC:
		ret = fg_read_vfsoc(client);
		break;

	default:
		ret = -1;
		break;
	}

	return ret;
}

static bool check_UV_charging_case(struct i2c_client *client)
{
	int fg_vcell = get_fuelgauge_value(client, FG_VOLTAGE);
	int fg_current = get_fuelgauge_value(client, FG_CURRENT);
	int threshold = 0;

	if (get_fuelgauge_value(client, FG_BATTERY_TYPE)
		== SDI_BATTERY_TYPE)
		threshold = 3300 + ((fg_current * 17) / 100);
	else if (get_fuelgauge_value(client, FG_BATTERY_TYPE)
		== ATL_BATTERY_TYPE)
		threshold = 3300 + ((fg_current * 13) / 100);

	if (fg_vcell <= threshold)
		return 1;
	else
		return 0;
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
	int recover_flag = 0;

	recover_flag = fg_check_cap_corruption(client);

	/* check VFcapacity every five minutes */
	if (!(fuelgauge->info.fg_chk_cnt++ % 10)) {
		fg_check_vf_fullcap_range(client);
		fuelgauge->info.fg_chk_cnt = 1;
	}

	fg_soc = get_fuelgauge_value(client, FG_LEVEL);
	if (fg_soc < 0) {
		pr_info("Can't read soc!!!");
		fg_soc = fuelgauge->info.soc;
	}

	if (!fuelgauge->pdata->check_jig_status() &&
		!fuelgauge->info.low_batt_comp_flag) {
		if (((fg_soc+5) < fuelgauge->info.previous_repsoc) ||
			(fg_soc > (fuelgauge->info.previous_repsoc+5)))
			fuelgauge->info.fg_skip = 1;
	}

	/* skip one time (maximum 30 seconds) because of corruption. */
	if (fuelgauge->info.fg_skip) {
		pr_info("%s: skip update until corruption check "
			"is done (fg_skip_cnt:%d)\n",
			__func__, ++fuelgauge->info.fg_skip_cnt);
		fg_soc = fuelgauge->info.soc;
		if (recover_flag || fuelgauge->info.fg_skip_cnt > 10) {
			fuelgauge->info.fg_skip = 0;
			fuelgauge->info.fg_skip_cnt = 0;
		}
	}

	if (fuelgauge->info.low_batt_boot_flag) {
		fg_soc = 0;

		if (fuelgauge->pdata->check_cable_callback() !=
			POWER_SUPPLY_TYPE_BATTERY &&
			!check_UV_charging_case(client)) {
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

	/* P4-Creative does not set full flag by force */
#if !defined(CONFIG_MACH_P4W_REV00)  && !defined(CONFIG_MACH_P4W_REV01)
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
			pr_info("%s: force fully charged SOC !! (%d)",
				__func__, fuelgauge->info.full_check_flag);
			fg_set_full_charged(client);
			fg_soc = get_fuelgauge_value(client, FG_LEVEL);
		} else if (fuelgauge->info.full_check_flag < 2)
			pr_info("%s: full_check_flag (%d)",
				__func__, fuelgauge->info.full_check_flag);

		/* prevent overflow */
		if (fuelgauge->info.full_check_flag++ > 10000)
			fuelgauge->info.full_check_flag = 3;
	} else
		fuelgauge->info.full_check_flag = 0;
#endif

	/*  Checks vcell level and tries to compensate SOC if needed.*/
	/*  If jig cable is connected, then skip low batt compensation check. */
	if (!fuelgauge->pdata->check_jig_status() &&
		value.intval == SEC_BATTERY_CHARGING_NONE)
		fg_soc = low_batt_compensation(
			client, fg_soc, fg_vcell, fg_current);

	if (fuelgauge->info.is_first_check)
		fuelgauge->info.is_first_check = false;

	fuelgauge->info.soc = fg_soc;
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
		pr_info("%s: full charge compensation start (avg_current %d)\n",
			__func__, avg_current);
		fg_fullcharged_compensation(fuelgauge->client,
			(int)(value.intval ==
			SEC_BATTERY_CHARGING_RECHARGING), 0);
	}
}

bool sec_hal_fg_init(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge =
				i2c_get_clientdata(client);

	fuelgauge->info.is_first_check = true;

	fg_set_battery_type(client);

	/* Init parameters to prevent wrong compensation. */
	fuelgauge->info.previous_fullcap =
		fg_read_register(client, FULLCAP_REG);
	fuelgauge->info.previous_vffullcap =
		fg_read_register(client, FULLCAP_NOM_REG);
	/* Init FullCAP of first full charging. */
	fuelgauge->info.full_charged_cap =
		fuelgauge->info.previous_fullcap;

	fuelgauge->info.previous_vfsoc =
		fg_read_vfsoc(client);
	fuelgauge->info.previous_repsoc =
		fg_read_soc(client);
	fuelgauge->info.previous_remcap =
		fg_read_register(client, REMCAP_REP_REG);
	fuelgauge->info.previous_mixcap =
		fg_read_register(client, REMCAP_MIX_REG);
	fuelgauge->info.previous_vfocv =
		fg_read_register(client, VFOCV_REG);
	fuelgauge->info.previous_fullcapacity =
		fuelgauge->info.previous_fullcap;
	fuelgauge->info.previous_vfcapacity =
		fuelgauge->info.previous_vffullcap;

	fg_read_model_data(client);
	fg_periodic_read(client);

	if (fuelgauge->pdata->check_cable_callback() ==
		POWER_SUPPLY_TYPE_BATTERY &&
		check_UV_charging_case(client))
		fuelgauge->info.low_batt_boot_flag = 1;

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
	return true;
}

bool sec_hal_fg_is_fuelalerted(struct i2c_client *client)
{
	return false;
}

bool sec_hal_fg_fuelalert_process(void *irq_data, bool is_fuel_alerted)
{
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
		(int)(value.intval == SEC_BATTERY_CHARGING_RECHARGING), 1);

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
			fuelgauge->info.low_batt_comp_flag = 0;
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
		pr_debug("%s: %s\n", __func__, str);
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
	u8 data[2];

	switch (offset) {
	case FG_REG:
		if (sscanf(buf, "%x\n", &x) == 1) {
			fg->reg_addr = x;
			fg_i2c_read(fg->client,
				fg->reg_addr, fg->reg_data, 2);
			pr_debug("%s: (read) addr = 0x%x, data = 0x%02x%02x\n",
				 __func__, fg->reg_addr,
				 fg->reg_data[1], fg->reg_data[0]);
		}
		ret = count;
		break;
	case FG_DATA:
		if (sscanf(buf, "%x\n", &x) == 1) {
			data[0] = (x & 0xFF00) >> 8;
			data[1] = (x & 0x00FF);
			pr_debug("%s: (write) addr = 0x%x, data = 0x%02x%02x\n",
				__func__, fg->reg_addr, data[1], data[0]);
			fg_i2c_write(fg->client,
				fg->reg_addr, data, 2);
		}
		ret = count;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
#endif

