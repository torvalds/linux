/*
 *  bq24190_charger.c
 *  Samsung BQ24190 Charger Driver
 *
 *  Copyright (C) 2012 Samsung Electronics
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DEBUG

#include <linux/battery/sec_charger.h>

static int bq24190_i2c_write(struct i2c_client *client, int reg, u8 *buf)
{
	int ret;
	ret = i2c_smbus_write_i2c_block_data(client, reg, 1, buf);
	if (ret < 0)
		dev_err(&client->dev, "%s: Error(%d)\n", __func__, ret);
	return ret;
}

static int bq24190_i2c_read(struct i2c_client *client, int reg, u8 *buf)
{
	int ret;
	ret = i2c_smbus_read_i2c_block_data(client, reg, 1, buf);
	if (ret < 0)
		dev_err(&client->dev, "%s: Error(%d)\n", __func__, ret);
	return ret;
}

#if 0
static void bq24190_i2c_write_array(struct i2c_client *client, u8 *buf,
				    int size)
{
	int i;
	for (i = 0; i < size; i += 3)
		bq24190_i2c_write(client, (u8) (*(buf + i)), (buf + i) + 1);
}
#endif

static int bq24190_read_regs(struct i2c_client *client, char *str)
{
	u8 data = 0;
	u32 addr = 0;

	if (!client)
		return -EINVAL;

	for (addr = 0; addr <= 0x0A; addr++) {
		bq24190_i2c_read(client, addr, &data);
		sprintf(str+strlen(str), "0x%02x", data);
		if (addr != 0x0A)
			sprintf(str+strlen(str), ", ");
	}

	return 0;
}

static int bq24190_test_read(struct i2c_client *client)
{
	struct sec_charger_info *charger;
	u8 data = 0;
	u32 addr = 0;

	if (!client)
		return -EINVAL;

	for (addr = 0; addr <= 0x0A; addr++) {
		bq24190_i2c_read(client, addr, &data);
		dev_info(&client->dev,
			"bq24190 addr : 0x%02x data : 0x%02x\n",
			addr, data);
	}

	charger = i2c_get_clientdata(client);

	dev_info(&client->dev,
		"bq24190 EN(%d), nCHG(%d), nCON(%d)\n",
		gpio_get_value(charger->pdata->chg_gpio_en),
		gpio_get_value(charger->pdata->chg_gpio_status),
		gpio_get_value(charger->pdata->bat_gpio_ta_nconnected));

	return 0;
}

static int bq24190_enable_charging(struct i2c_client *client)
{
	struct sec_charger_info *charger;
	u8 data = 0;

	if (!client)
		return -EINVAL;

	charger = i2c_get_clientdata(client);

	/* Set CE pin to LOW */
	if (charger->pdata->chg_gpio_en)
		gpio_set_value(charger->pdata->chg_gpio_en,
			charger->pdata->chg_polarity_en ? 1 : 0);

	/* Set register */
	bq24190_i2c_read(client, BQ24190_REG_PWRON_CFG, &data);
	data = data & ~(0x3 << 4) | (0x1 << 4);
	bq24190_i2c_write(client, BQ24190_REG_PWRON_CFG, &data);

	return 0;
}

static int bq24190_disable_charging(struct i2c_client *client)
{
	struct sec_charger_info *charger;
	u8 data = 0;

	if (!client)
		return -EINVAL;

	charger = i2c_get_clientdata(client);

	/* Set CE pin to HIGH */
	if (charger->pdata->chg_gpio_en)
		gpio_set_value(charger->pdata->chg_gpio_en,
			charger->pdata->chg_polarity_en ? 0 : 1);

	/* Set register */
	bq24190_i2c_read(client, BQ24190_REG_PWRON_CFG, &data);
	data = data & ~(0x3 << 4);
	bq24190_i2c_write(client, BQ24190_REG_PWRON_CFG, &data);

	return 0;
}

static int bq24190_get_status(struct i2c_client *client, u8 *status)
{
	u8 data = 0;

	if (!client)
		return -EINVAL;

	bq24190_i2c_read(client, BQ24190_REG_STATUS, &data);

	*status = data;

	/* [7:6] VBUS_STAT */
	switch (data >> 6) {
	case 0:
		dev_dbg(&client->dev,
			"%s: 0x%02x VBUS_STAT: Unknown\n",
			__func__, data);
		break;
	case 1:
		dev_dbg(&client->dev,
			"%s: 0x%02x VBUS_STAT: USB host\n",
			__func__, data);
		break;
	case 2:
		dev_dbg(&client->dev,
			"%s: 0x%02x VBUS_STAT: Adapter port\n",
			__func__, data);
		break;
	case 3:
		dev_dbg(&client->dev,
			"%s: 0x%02x VBUS_STAT: OTG\n",
			__func__, data);
		break;
	}

	/* [5:4] CHRG_STAT */
	switch ((data & (0x3 << 4)) >> 4) {
	case 0:
		dev_dbg(&client->dev,
			"%s: 0x%02x CHRG_STAT: Not Charging\n",
			__func__, data);
		break;
	case 1:
		dev_dbg(&client->dev,
			"%s: 0x%02x CHRG_STAT: Pre-charge\n",
			__func__, data);
		break;
	case 2:
		dev_dbg(&client->dev,
			"%s: 0x%02x CHRG_STAT: Fast Charging\n",
			__func__, data);
		break;
	case 3:
		dev_dbg(&client->dev,
			"%s: 0x%02x CHRG_STAT: Charge Done\n",
			__func__, data);
		break;
	}

	/* [3] DPM_STAT */
	switch ((data & (0x1 << 3)) >> 3) {
	case 0:
		dev_dbg(&client->dev,
			"%s: 0x%02x DPM_STAT: Not DPM\n",
			__func__, data);
		break;
	case 1:
		dev_dbg(&client->dev,
			"%s: 0x%02x DPM_STAT: VINDPM or IINDPM\n",
			__func__, data);
		break;
	}

	/* [2] PG_STAT */
	switch ((data & (0x1 << 2)) >> 2) {
	case 0:
		dev_dbg(&client->dev,
			"%s: 0x%02x PG_STAT: Not Power Good\n",
			__func__, data);
		break;
	case 1:
		dev_dbg(&client->dev,
			"%s: 0x%02x PG_STAT: Power Good\n",
			__func__, data);
		break;
	}

	/* [1] THERM_STAT */
	switch ((data & (0x1 << 1)) >> 1) {
	case 0:
		dev_dbg(&client->dev,
			"%s: 0x%02x THERM_STAT: Normal\n",
			__func__, data);
		break;
	case 1:
		dev_dbg(&client->dev,
			"%s: 0x%02x THERM_STAT: TREG\n",
			__func__, data);
		break;
	}

	/* [0] VSYS_STAT */
	switch (data & 0x1) {
	case 0:
		dev_dbg(&client->dev,
			"%s: 0x%02x VSYS_STAT: BAT > VSYSMIN\n",
			__func__, data);
		break;
	case 1:
		dev_dbg(&client->dev,
			"%s: 0x%02x VSYS_STAT: Battery is too low\n",
			__func__, data);
		break;
	}

	return 0;
}

static int bq24190_get_fault(struct i2c_client *client, u8 *fault)
{
	u8 data = 0;

	if (!client)
		return -EINVAL;

	bq24190_i2c_read(client, BQ24190_REG_FAULT, &data);

	*fault = data;

	if (data == 0x00)
		dev_dbg(&client->dev, "%s: 0x%02x No fault\n",
			__func__, data);
	else {
		if (data & (0x1 << 7))
			dev_info(&client->dev,
				"%s: 0x%02x watchdog timer expiration\n",
				__func__, data);
		if (data & (0x1 << 6))
			dev_info(&client->dev,
				"%s: 0x%02x OTG fault\n", __func__, data);
		if (data & (0x1 << 4))
			dev_info(&client->dev,
				"%s: 0x%02x Input fault (OVP or bad source)\n",
				__func__, data);
		if (data & (0x2 << 4))
			dev_info(&client->dev,
				"%s: 0x%02x Thermal shutdown\n",
				__func__, data);
		if (data & (0x3 << 4))
			dev_info(&client->dev,
				"%s: 0x%02x Charge timer expiration\n",
				__func__, data);
		if (data & (0x1 << 3))
			dev_info(&client->dev,
				"%s: 0x%02x System OVP\n", __func__, data);
		if (data & (0x1 << 0))
			dev_info(&client->dev,
				"%s: 0x%02x TS1 Cold\n", __func__, data);
		if (data & (0x2 << 0))
			dev_info(&client->dev,
				"%s: 0x%02x TS1 Hot\n", __func__, data);
		if (data & (0x3 << 0))
			dev_info(&client->dev,
				"%s: 0x%02x TS2 Cold\n", __func__, data);
		if (data & (0x4 << 0))
			dev_info(&client->dev,
				"%s: 0x%02x TS2 Hot\n", __func__, data);
		if (data & (0x5 << 0))
			dev_info(&client->dev,
				"%s: 0x%02x Both Cold\n", __func__, data);
		if (data & (0x6 << 0))
			dev_info(&client->dev,
				"%s: 0x%02x Both Hot\n", __func__, data);
		if (data & (0x7 << 0))
			dev_info(&client->dev,
				"%s: 0x%02x one Hot one Cold\n",
				__func__, data);
	}

	return 0;
}

static int bq24190_get_charging_status(struct i2c_client *client)
{
	struct sec_charger_info *charger;
	int status = POWER_SUPPLY_STATUS_UNKNOWN;
	u8 data_status = 0;
	u8 data_fault = 0;

	if (!client)
		return -EINVAL;

	charger = i2c_get_clientdata(client);

	if (bq24190_get_status(client, &data_status) < 0)
		dev_err(&client->dev, "%s: fail to get status\n", __func__);

	if (bq24190_get_fault(client, &data_fault) < 0)
		dev_err(&client->dev, "%s: fail to get fault\n", __func__);

#if 0	/* CHECK ME */
	/* At least one charge cycle terminated,
	 *Charge current < Termination Current
	 */
	if ((data_status & BQ24190_CHARGING_DONE) == 0x30)
		status = POWER_SUPPLY_STATUS_FULL;
	goto charging_status_end;
	if (data_status & BQ24190_CHARGING_ENABLE)
		status = POWER_SUPPLY_STATUS_CHARGING;
	goto charging_status_end;
	if (data_fault)

		/* if error bit check, ignore the status of charger-ic */
		status = POWER_SUPPLY_STATUS_DISCHARGING;
charging_status_end:
#endif

	return (int)status;
}

static int bq24190_get_charging_health(struct i2c_client *client)
{
	int health = POWER_SUPPLY_HEALTH_GOOD;
	u8 data_fault = 0;

	if (!client)
		return -EINVAL;

	if (bq24190_get_fault(client, &data_fault) < 0)
		dev_err(&client->dev, "%s: fail to get fault\n", __func__);

#if 0	/* CHECK ME */
	if (data_fault)
		pr_info("%s : Fault (0x%02x)\n", __func__, data_fault);
	if ((data_fault & BQ24190_CHARGING_DONE) == 0x20)
		health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	goto charging_health_end;
	if (data_fault)

		/* if error bit check, ignore the status of charger-ic */
		health = POWER_SUPPLY_HEALTH_GOOD;
charging_health_end:
#endif

	return health;
}

static int bq24190_get_charging_current(struct i2c_client *client)
{
	u8 data = 0;
	int ichg = 500;

	if (!client)
		return -EINVAL;

	bq24190_i2c_read(client, BQ24190_REG_CHRG_C, &data);

	if (data & (0x1 << 7))
		ichg += 2048;
	if (data & (0x1 << 6))
		ichg += 1024;
	if (data & (0x1 << 5))
		ichg += 512;
	if (data & (0x1 << 4))
		ichg += 256;
	if (data & (0x1 << 3))
		ichg += 128;
	if (data & (0x1 << 2))
		ichg += 64;

	return ichg;
}

static int bq24190_set_charging_current(
				struct i2c_client *client, int set_current)
{
	u8 data = 0;

	if (!client)
		return -EINVAL;

	if (set_current > 500) {

		/* High-current mode */
		data = 0x48;
		bq24190_i2c_write(client, BQ24190_REG_CHRG_C, &data);
		udelay(10);
	} else {

		/* USB5 */
		data = 0x00;
		bq24190_i2c_write(client, BQ24190_REG_CHRG_C, &data);
		udelay(10);
	}

	dev_dbg(&client->dev, "%s: Set charging current as %dmA.\n",
		__func__, set_current);

	return 0;
}

static int bq24190_set_input_current_limit(struct i2c_client *client,
					     int charger_type)
{
	u8 data = 0;

	if (!client)
		return -EINVAL;

	bq24190_i2c_read(client, BQ24190_REG_INSRC, &data);
	data = data & ~(0x7);	/* clear IINLIM bits */

	switch (charger_type) {
	case POWER_SUPPLY_TYPE_MAINS:
		/* 2A (110) */
		data = data | (0x6);
		break;
	case POWER_SUPPLY_TYPE_USB:
	default:
		/* 500mA (010) */
		data = data | (0x2);
		break;
	}

	bq24190_i2c_write(client, BQ24190_REG_INSRC, &data);

	return 0;
}

static int bq24190_set_fast_charge_current(struct i2c_client *client,
					     int charger_type)
{
	u8 data = 0;

	if (!client)
		return -EINVAL;

	bq24190_i2c_read(client, BQ24190_REG_CHRG_C, &data);
	data = data & ~(0x3F << 2);	/* clear ICHG bits (111111) */

	switch (charger_type) {
	case POWER_SUPPLY_TYPE_MAINS:
		/* 2036mA (011000) */
		data = data | (0x18 << 2);
		break;
	case POWER_SUPPLY_TYPE_USB:
	default:
		/* 500mA (000000) */
		data = data | (0x0 << 2);
		break;
	}

	bq24190_i2c_write(client, BQ24190_REG_CHRG_C, &data);

	return 0;
}

static int bq24190_set_termination_current(struct i2c_client *client,
					     int charger_type)
{
	u8 data = 0;

	if (!client)
		return -EINVAL;

	/* Set termination current */
	bq24190_i2c_read(client, BQ24190_REG_PCHRG_TRM_C, &data);
	data = data & ~(0xF);	/* clear ITERM bits */
	data = data | 0x1;	/* 256mA (0001) */
	bq24190_i2c_write(client, BQ24190_REG_PCHRG_TRM_C, &data);

	/* Enable charge termination */
	bq24190_i2c_read(client, BQ24190_REG_CHRG_TRM_TMR, &data);
	data = data & ~(0x1 << 7);	/* clear EN_TERM bit */
	data = data | (0x1 << 7);	/* Enable termination */
	bq24190_i2c_write(client, BQ24190_REG_CHRG_TRM_TMR, &data);

	return 0;
}

static int bq24190_charger_function_control(struct i2c_client *client,
					     int charger_type)
{
	struct sec_charger_info *charger;

	if (!client)
		return -EINVAL;

	charger = i2c_get_clientdata(client);

	if (charger_type == POWER_SUPPLY_TYPE_BATTERY) {
		/* No charger */
		bq24190_disable_charging(client);
		charger->is_charging = false;
	} else {
		/* Set charging current by charger type */
		bq24190_set_input_current_limit(client, charger_type);
		bq24190_set_fast_charge_current(client, charger_type);
		bq24190_set_termination_current(client, charger_type);
		bq24190_enable_charging(client);
	}

	return 0;
}

static int bq24190_disable_timer(struct i2c_client *client)
{
	u8 data = 0;

	if (!client)
		return -EINVAL;

	dev_dbg(&client->dev, "%s\n", __func__);

	/* Disable watchdog timer and safety timer */
	bq24190_i2c_read(client, BQ24190_REG_CHRG_TRM_TMR, &data);
	data = data & ~(0x30);
	data = data & ~(0x08);
	bq24190_i2c_write(client, BQ24190_REG_CHRG_TRM_TMR, &data);

	return 0;
}

static int bq24190_set_minimum_system_voltage(struct i2c_client *client)
{
	u8 data = 0;

	if (!client)
		return -EINVAL;

	dev_dbg(&client->dev, "%s\n", __func__);

	/* 3.0V */
	bq24190_i2c_read(client, BQ24190_REG_PWRON_CFG, &data);
	data = data & ~(0x7 << 1);	/* Clear SYS_MIN bits */
	bq24190_i2c_write(client, BQ24190_REG_PWRON_CFG, &data);

	return 0;
}

bool sec_hal_chg_init(struct i2c_client *client)
{
	bq24190_disable_timer(client);
	bq24190_set_minimum_system_voltage(client);
	bq24190_test_read(client);
	return true;
}

bool sec_hal_chg_suspend(struct i2c_client *client)
{
	return true;
}

bool sec_hal_chg_resume(struct i2c_client *client)
{
	return true;
}

bool sec_hal_chg_get_property(struct i2c_client *client,
			      enum power_supply_property psp,
			      union power_supply_propval *val)
{
	struct sec_charger_info *charger;

	if (!client)
		return false;

	charger = i2c_get_clientdata(client);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = bq24190_get_charging_status(client);
		dev_dbg(&client->dev, "%s: STATUS, %d\n",
			__func__, val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = bq24190_get_charging_health(client);
		dev_dbg(&client->dev, "%s: HEALTH, %d\n",
			__func__, val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (charger->charging_current)
			val->intval = bq24190_get_charging_current(client);

		else
			val->intval = 0;
		dev_dbg(&client->dev, "%s: CURRENT_NOW, %d\n",
			__func__, val->intval);
		break;
	default:
		return false;
	}

	return true;
}

bool sec_hal_chg_set_property(struct i2c_client *client,
			      enum power_supply_property psp,
			      const union power_supply_propval *val)
{
	struct sec_charger_info *charger;

	if (!client)
		return false;

	charger = i2c_get_clientdata(client);

	switch (psp) {
	/* val->intval : type */
	case POWER_SUPPLY_PROP_ONLINE:
		dev_dbg(&client->dev, "%s: ONLINE, %d\n",
			__func__, val->intval);
		bq24190_charger_function_control(client, val->intval);
		bq24190_test_read(client);
		break;
	/* val->intval : charging current */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		dev_dbg(&client->dev, "%s: CURRENT_NOW, %d\n",
			__func__, val->intval);
		bq24190_set_charging_current(client, val->intval);
		bq24190_test_read(client);
		break;
	default:
		return false;
	}

	return true;
}

ssize_t sec_hal_chg_show_attrs(struct device *dev,
				const ptrdiff_t offset, char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sec_charger_info *chg =
		container_of(psy, struct sec_charger_info, psy_chg);
	int i = 0;
	char *str = NULL;

	switch (offset) {
/*	case CHG_REG: */
/*		break; */
	case CHG_DATA:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%x\n",
			chg->reg_data);
		break;
	case CHG_REGS:
		str = kzalloc(sizeof(char)*1024, GFP_KERNEL);
		if (!str)
			return -ENOMEM;

		bq24190_read_regs(chg->client, str);
		dev_dbg(&chg->client->dev, "%s: %s\n", __func__, str);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n",
			str);

		kfree(str);
		break;
	default:
		i = -EINVAL;
	}

	return i;
}

ssize_t sec_hal_chg_store_attrs(struct device *dev,
				const ptrdiff_t offset,
				const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sec_charger_info *chg =
		container_of(psy, struct sec_charger_info, psy_chg);
	int ret = 0;
	int x = 0;
	u8 data = 0;

	switch (offset) {
	case CHG_REG:
		if (sscanf(buf, "%x\n", &x) == 1) {
			chg->reg_addr = x;
			bq24190_i2c_read(chg->client, chg->reg_addr, &data);
			chg->reg_data = data;
			dev_dbg(&chg->client->dev,
				"%s: (read) addr = 0x%x, data = 0x%x\n",
				__func__, chg->reg_addr, chg->reg_data);
			ret = count;
		}
		break;
	case CHG_DATA:
		if (sscanf(buf, "%x\n", &x) == 1) {
			data = (u8)x;
			dev_dbg(&chg->client->dev,
				"%s: (write) addr = 0x%x, data = 0x%x\n",
				__func__, chg->reg_addr, data);
			bq24190_i2c_write(chg->client,
				chg->reg_addr, &data);
			ret = count;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

