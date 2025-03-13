// SPDX-License-Identifier: GPL-2.0
/*
 * stc3117_fuel_gauge.c - STMicroelectronics STC3117 Fuel Gauge Driver
 *
 * Copyright (c) 2024 Silicon Signals Pvt Ltd.
 * Author:      Hardevsinh Palaniya <hardevsinh.palaniya@siliconsignals.io>
 *              Bhavin Sharma <bhavin.sharma@siliconsignals.io>
 */

#include <linux/crc8.h>
#include <linux/devm-helpers.h>
#include <linux/i2c.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#define STC3117_ADDR_MODE                      0x00
#define STC3117_ADDR_CTRL                      0x01
#define STC3117_ADDR_SOC_L                     0x02
#define STC3117_ADDR_SOC_H                     0x03
#define STC3117_ADDR_COUNTER_L                 0x04
#define STC3117_ADDR_COUNTER_H                 0x05
#define STC3117_ADDR_CURRENT_L                 0x06
#define STC3117_ADDR_CURRENT_H                 0x07
#define STC3117_ADDR_VOLTAGE_L                 0x08
#define STC3117_ADDR_VOLTAGE_H                 0x09
#define STC3117_ADDR_TEMPERATURE               0x0A
#define STC3117_ADDR_AVG_CURRENT_L             0x0B
#define STC3117_ADDR_AVG_CURRENT_H             0x0C
#define STC3117_ADDR_OCV_L                     0x0D
#define STC3117_ADDR_OCV_H                     0x0E
#define STC3117_ADDR_CC_CNF_L                  0x0F
#define STC3117_ADDR_CC_CNF_H                  0x10
#define STC3117_ADDR_VM_CNF_L                  0x11
#define STC3117_ADDR_VM_CNF_H                  0x12
#define STC3117_ADDR_ALARM_soc                 0x13
#define STC3117_ADDR_ALARM_VOLTAGE             0x14
#define STC3117_ADDR_ID                        0x18
#define STC3117_ADDR_CC_ADJ_L			0x1B
#define STC3117_ADDR_CC_ADJ_H			0x1C
#define STC3117_ADDR_VM_ADJ_L			0x1D
#define STC3117_ADDR_VM_ADJ_H			0x1E
#define STC3117_ADDR_RAM			0x20
#define STC3117_ADDR_OCV_TABLE			0x30
#define STC3117_ADDR_SOC_TABLE			0x30

/* Bit mask definition */
#define STC3117_ID			        0x16
#define STC3117_MIXED_MODE			0x00
#define STC3117_VMODE				BIT(0)
#define STC3117_GG_RUN				BIT(4)
#define STC3117_CC_MODE			BIT(5)
#define STC3117_BATFAIL			BIT(3)
#define STC3117_PORDET				BIT(4)
#define STC3117_RAM_SIZE			16
#define STC3117_OCV_TABLE_SIZE			16
#define STC3117_RAM_TESTWORD			0x53A9
#define STC3117_SOFT_RESET                     0x11
#define STC3117_NOMINAL_CAPACITY		2600

#define VOLTAGE_LSB_VALUE			9011
#define CURRENT_LSB_VALUE			24084
#define APP_CUTOFF_VOLTAGE			2500
#define MAX_HRSOC				51200
#define MAX_SOC				1000
#define CHG_MIN_CURRENT			200
#define CHG_END_CURRENT			20
#define APP_MIN_CURRENT			(-5)
#define BATTERY_FULL				95
#define CRC8_POLYNOMIAL			0x07
#define CRC8_INIT				0x00

DECLARE_CRC8_TABLE(stc3117_crc_table);

enum stc3117_state {
	STC3117_INIT,
	STC3117_RUNNING,
	STC3117_POWERDN,
};

/* Default ocv curve Li-ion battery */
static const int ocv_value[16] = {
	3400, 3582, 3669, 3676, 3699, 3737, 3757, 3774,
	3804, 3844, 3936, 3984, 4028, 4131, 4246, 4320
};

union stc3117_internal_ram {
	u8 ram_bytes[STC3117_RAM_SIZE];
	struct {
	u16 testword;   /* 0-1    Bytes */
	u16 hrsoc;      /* 2-3    Bytes */
	u16 cc_cnf;     /* 4-5    Bytes */
	u16 vm_cnf;     /* 6-7    Bytes */
	u8 soc;         /* 8      Byte  */
	u8 state;       /* 9      Byte  */
	u8 unused[5];   /* 10-14  Bytes */
	u8 crc;         /* 15     Byte  */
	} reg;
};

struct stc3117_battery_info {
	int voltage_min_mv;
	int voltage_max_mv;
	int battery_capacity_mah;
	int sense_resistor;
};

struct stc3117_data {
	struct i2c_client *client;
	struct regmap *regmap;
	struct delayed_work update_work;
	struct power_supply *battery;
	union stc3117_internal_ram ram_data;
	struct stc3117_battery_info battery_info;

	u8 soc_tab[16];
	int cc_cnf;
	int vm_cnf;
	int cc_adj;
	int vm_adj;
	int avg_current;
	int avg_voltage;
	int batt_current;
	int voltage;
	int temp;
	int soc;
	int ocv;
	int hrsoc;
	int presence;
};

static int stc3117_convert(int value, int factor)
{
	value = (value * factor) / 4096;
	return value * 1000;
}

static int stc3117_get_battery_data(struct stc3117_data *data)
{
	u8 reg_list[16];
	u8 data_adjust[4];
	int value, mode;

	regmap_bulk_read(data->regmap, STC3117_ADDR_MODE,
			 reg_list, sizeof(reg_list));

	/* soc */
	value = (reg_list[3] << 8) + reg_list[2];
	data->hrsoc = value;
	data->soc = (value * 10 + 256) / 512;

	/* current in uA*/
	value = (reg_list[7] << 8) + reg_list[6];
	data->batt_current = stc3117_convert(value,
			CURRENT_LSB_VALUE / data->battery_info.sense_resistor);

	/* voltage in uV */
	value = (reg_list[9] << 8) + reg_list[8];
	data->voltage = stc3117_convert(value, VOLTAGE_LSB_VALUE);

	/* temp in 1/10 °C */
	data->temp = reg_list[10] * 10;

	/* Avg current in uA */
	value = (reg_list[12] << 8) + reg_list[11];
	regmap_read(data->regmap, STC3117_ADDR_MODE, &mode);
	if (!(mode & STC3117_VMODE)) {
		value = stc3117_convert(value,
			CURRENT_LSB_VALUE / data->battery_info.sense_resistor);
		value = value / 4;
	} else {
		value = stc3117_convert(value, 36 * STC3117_NOMINAL_CAPACITY);
	}
	data->avg_current = value;

	/* ocv in uV */
	value = (reg_list[14] << 8) + reg_list[13];
	value = stc3117_convert(value, VOLTAGE_LSB_VALUE);
	value = (value + 2) / 4;
	data->ocv = value;

	/* CC & VM adjustment counters */
	regmap_bulk_read(data->regmap, STC3117_ADDR_CC_ADJ_L,
			 data_adjust, sizeof(data_adjust));
	value = (data_adjust[1] << 8) + data_adjust[0];
	data->cc_adj = value;

	value = (data_adjust[3] << 8) + data_adjust[2];
	data->vm_adj = value;

	return 0;
}

static int ram_write(struct stc3117_data *data)
{
	int ret;

	ret = regmap_bulk_write(data->regmap, STC3117_ADDR_RAM,
				data->ram_data.ram_bytes, STC3117_RAM_SIZE);
	if (ret)
		return ret;

	return 0;
};

static int ram_read(struct stc3117_data *data)
{
	int ret;

	ret = regmap_bulk_read(data->regmap, STC3117_ADDR_RAM,
			       data->ram_data.ram_bytes, STC3117_RAM_SIZE);
	if (ret)
		return ret;

	return 0;
};

static int stc3117_set_para(struct stc3117_data *data)
{
	int ret;

	ret = regmap_write(data->regmap, STC3117_ADDR_MODE, STC3117_VMODE);

	for (int i = 0; i < STC3117_OCV_TABLE_SIZE; i++)
		ret |= regmap_write(data->regmap, STC3117_ADDR_OCV_TABLE + i,
			     ocv_value[i] * 100 / 55);
	if (data->soc_tab[1] != 0)
		ret |= regmap_bulk_write(data->regmap, STC3117_ADDR_SOC_TABLE,
				  data->soc_tab, STC3117_OCV_TABLE_SIZE);

	ret |= regmap_write(data->regmap, STC3117_ADDR_CC_CNF_H,
				(data->ram_data.reg.cc_cnf >> 8) & 0xFF);

	ret |= regmap_write(data->regmap, STC3117_ADDR_CC_CNF_L,
					data->ram_data.reg.cc_cnf & 0xFF);

	ret |= regmap_write(data->regmap, STC3117_ADDR_VM_CNF_H,
				(data->ram_data.reg.vm_cnf >> 8) & 0xFF);

	ret |= regmap_write(data->regmap, STC3117_ADDR_VM_CNF_L,
					data->ram_data.reg.vm_cnf & 0xFF);

	ret |= regmap_write(data->regmap, STC3117_ADDR_CTRL, 0x03);

	ret |= regmap_write(data->regmap, STC3117_ADDR_MODE,
					STC3117_MIXED_MODE | STC3117_GG_RUN);

	return ret;
};

static int stc3117_init(struct stc3117_data *data)
{
	int id, ret;
	int ctrl;
	int ocv_m, ocv_l;

	regmap_read(data->regmap, STC3117_ADDR_ID, &id);
	if (id != STC3117_ID)
		return -EINVAL;

	data->cc_cnf = (data->battery_info.battery_capacity_mah *
			data->battery_info.sense_resistor * 250 + 6194) / 12389;
	data->vm_cnf = (data->battery_info.battery_capacity_mah
						* 200 * 50 + 24444) / 48889;

	/* Battery has not been removed */
	data->presence = 1;

	/* Read RAM data */
	ret = ram_read(data);
	if (ret)
		return ret;

	if (data->ram_data.reg.testword != STC3117_RAM_TESTWORD ||
	    (crc8(stc3117_crc_table, data->ram_data.ram_bytes,
					STC3117_RAM_SIZE, CRC8_INIT)) != 0) {
		data->ram_data.reg.testword = STC3117_RAM_TESTWORD;
		data->ram_data.reg.cc_cnf = data->cc_cnf;
		data->ram_data.reg.vm_cnf = data->vm_cnf;
		data->ram_data.reg.crc = crc8(stc3117_crc_table,
						data->ram_data.ram_bytes,
						STC3117_RAM_SIZE - 1, CRC8_INIT);

		ret = regmap_read(data->regmap, STC3117_ADDR_OCV_H, &ocv_m);

		ret |= regmap_read(data->regmap, STC3117_ADDR_OCV_L, &ocv_l);

		ret |= stc3117_set_para(data);

		ret |= regmap_write(data->regmap, STC3117_ADDR_OCV_H, ocv_m);

		ret |= regmap_write(data->regmap, STC3117_ADDR_OCV_L, ocv_l);
		if (ret)
			return ret;
	} else {
		ret = regmap_read(data->regmap, STC3117_ADDR_CTRL, &ctrl);
		if (ret)
			return ret;

		if ((ctrl & STC3117_BATFAIL) != 0  ||
		    (ctrl & STC3117_PORDET) != 0) {
			ret = regmap_read(data->regmap,
					  STC3117_ADDR_OCV_H, &ocv_m);

			ret |= regmap_read(data->regmap,
						STC3117_ADDR_OCV_L, &ocv_l);

			ret |= stc3117_set_para(data);

			ret |= regmap_write(data->regmap,
						STC3117_ADDR_OCV_H, ocv_m);

			ret |= regmap_write(data->regmap,
						STC3117_ADDR_OCV_L, ocv_l);
			if (ret)
				return ret;
		} else {
			ret = stc3117_set_para(data);
			ret |= regmap_write(data->regmap, STC3117_ADDR_SOC_H,
				     (data->ram_data.reg.hrsoc >> 8 & 0xFF));
			ret |= regmap_write(data->regmap, STC3117_ADDR_SOC_L,
				     (data->ram_data.reg.hrsoc & 0xFF));
			if (ret)
				return ret;
		}
	}

	data->ram_data.reg.state = STC3117_INIT;
	data->ram_data.reg.crc = crc8(stc3117_crc_table,
					data->ram_data.ram_bytes,
					STC3117_RAM_SIZE - 1, CRC8_INIT);
	ret = ram_write(data);
	if (ret)
		return ret;

	return 0;
};

static int stc3117_task(struct stc3117_data *data)
{
	int id, mode, ret;
	int count_l, count_m;
	int ocv_l, ocv_m;

	regmap_read(data->regmap, STC3117_ADDR_ID, &id);
	if (id != STC3117_ID) {
		data->presence = 0;
		return -EINVAL;
	}

	stc3117_get_battery_data(data);

	/* Read RAM data */
	ret = ram_read(data);
	if (ret)
		return ret;

	if (data->ram_data.reg.testword != STC3117_RAM_TESTWORD ||
	    (crc8(stc3117_crc_table, data->ram_data.ram_bytes,
					STC3117_RAM_SIZE, CRC8_INIT) != 0)) {
		data->ram_data.reg.testword = STC3117_RAM_TESTWORD;
		data->ram_data.reg.cc_cnf = data->cc_cnf;
		data->ram_data.reg.vm_cnf = data->vm_cnf;
		data->ram_data.reg.crc = crc8(stc3117_crc_table,
						data->ram_data.ram_bytes,
						STC3117_RAM_SIZE - 1, CRC8_INIT);
		data->ram_data.reg.state = STC3117_INIT;
	}

	/* check battery presence status */
	ret = regmap_read(data->regmap, STC3117_ADDR_CTRL, &mode);
	if ((mode & STC3117_BATFAIL) != 0) {
		data->presence = 0;
		data->ram_data.reg.testword = 0;
		data->ram_data.reg.state = STC3117_INIT;
		ret = ram_write(data);
		ret |= regmap_write(data->regmap, STC3117_ADDR_CTRL, STC3117_PORDET);
		if (ret)
			return ret;
	}

	data->presence = 1;

	ret = regmap_read(data->regmap, STC3117_ADDR_MODE, &mode);
	if (ret)
		return ret;
	if ((mode & STC3117_GG_RUN) == 0) {
		if (data->ram_data.reg.state > STC3117_INIT) {
			ret = stc3117_set_para(data);

			ret |= regmap_write(data->regmap, STC3117_ADDR_SOC_H,
					(data->ram_data.reg.hrsoc >> 8 & 0xFF));
			ret |= regmap_write(data->regmap, STC3117_ADDR_SOC_L,
					(data->ram_data.reg.hrsoc & 0xFF));
			if (ret)
				return ret;
		} else {
			ret = regmap_read(data->regmap, STC3117_ADDR_OCV_H, &ocv_m);

			ret |= regmap_read(data->regmap, STC3117_ADDR_OCV_L, &ocv_l);

			ret |= stc3117_set_para(data);

			ret |= regmap_write(data->regmap, STC3117_ADDR_OCV_H, ocv_m);

			ret |= regmap_write(data->regmap, STC3117_ADDR_OCV_L, ocv_l);
			if (ret)
				return ret;
		}
		data->ram_data.reg.state = STC3117_INIT;
	}

	regmap_read(data->regmap, STC3117_ADDR_COUNTER_L, &count_l);
	regmap_read(data->regmap, STC3117_ADDR_COUNTER_H, &count_m);

	count_m = (count_m << 8) + count_l;

	/* INIT state, wait for batt_current & temperature value available: */
	if (data->ram_data.reg.state == STC3117_INIT && count_m > 4) {
		data->avg_voltage = data->voltage;
		data->avg_current = data->batt_current;
		data->ram_data.reg.state = STC3117_RUNNING;
	}

	if (data->ram_data.reg.state != STC3117_RUNNING) {
		data->batt_current = -ENODATA;
		data->temp = -ENODATA;
	} else {
		if (data->voltage < APP_CUTOFF_VOLTAGE)
			data->soc = -ENODATA;

		if (mode & STC3117_VMODE) {
			data->avg_current = -ENODATA;
			data->batt_current = -ENODATA;
		}
	}

	data->ram_data.reg.hrsoc = data->hrsoc;
	data->ram_data.reg.soc = (data->soc + 5) / 10;
	data->ram_data.reg.crc = crc8(stc3117_crc_table,
					data->ram_data.ram_bytes,
					STC3117_RAM_SIZE - 1, CRC8_INIT);

	ret = ram_write(data);
	if (ret)
		return ret;
	return 0;
};

static void fuel_gauge_update_work(struct work_struct *work)
{
	struct stc3117_data *data =
		container_of(work, struct stc3117_data, update_work.work);

	stc3117_task(data);

	/* Schedule the work to run again in 2 seconds */
	schedule_delayed_work(&data->update_work, msecs_to_jiffies(2000));
}

static int stc3117_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct stc3117_data *data = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (data->soc > BATTERY_FULL)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (data->batt_current < 0)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (data->batt_current > 0)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = data->voltage;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = data->batt_current;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = data->ocv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = data->avg_current;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = data->soc;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = data->temp;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = data->presence;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property stc3117_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_PRESENT,
};

static const struct power_supply_desc stc3117_battery_desc = {
	.name = "stc3117-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.get_property = stc3117_get_property,
	.properties = stc3117_battery_props,
	.num_properties = ARRAY_SIZE(stc3117_battery_props),
};

static const struct regmap_config stc3117_regmap_config = {
	.reg_bits       = 8,
	.val_bits       = 8,
};

static int stc3117_probe(struct i2c_client *client)
{
	struct stc3117_data *data;
	struct power_supply_config psy_cfg = {};
	struct power_supply_battery_info *info;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->regmap = devm_regmap_init_i2c(client, &stc3117_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	psy_cfg.drv_data = data;
	psy_cfg.fwnode = dev_fwnode(&client->dev);

	crc8_populate_msb(stc3117_crc_table, CRC8_POLYNOMIAL);

	data->battery = devm_power_supply_register(&client->dev,
					&stc3117_battery_desc, &psy_cfg);
	if (IS_ERR(data->battery))
		return dev_err_probe(&client->dev, PTR_ERR(data->battery),
					"failed to register battery\n");

	ret = device_property_read_u32(&client->dev, "shunt-resistor-micro-ohms",
					&data->battery_info.sense_resistor);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				"failed to get shunt-resistor-micro-ohms\n");
	data->battery_info.sense_resistor = data->battery_info.sense_resistor / 1000;

	ret = power_supply_get_battery_info(data->battery, &info);
	if (ret)
		return dev_err_probe(&client->dev, ret,
					"failed to get battery information\n");

	data->battery_info.battery_capacity_mah = info->charge_full_design_uah / 1000;
	data->battery_info.voltage_min_mv = info->voltage_min_design_uv / 1000;
	data->battery_info.voltage_max_mv = info->voltage_max_design_uv / 1000;

	ret = stc3117_init(data);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				"failed to initialize of stc3117\n");

	ret = devm_delayed_work_autocancel(&client->dev, &data->update_work,
					   fuel_gauge_update_work);
	if (ret)
		return ret;

	schedule_delayed_work(&data->update_work, 0);

	return 0;
}

static const struct i2c_device_id stc3117_id[] = {
	{ "stc3117", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, stc3117_id);

static const struct of_device_id stc3117_of_match[] = {
	{ .compatible = "st,stc3117" },
	{ }
};
MODULE_DEVICE_TABLE(of, stc3117_of_match);

static struct i2c_driver stc3117_i2c_driver = {
	.driver = {
		.name = "stc3117_i2c_driver",
		.of_match_table = stc3117_of_match,
	},
	.probe = stc3117_probe,
	.id_table = stc3117_id,
};

module_i2c_driver(stc3117_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hardevsinh Palaniya <hardevsinh.palaniya@siliconsignals.io>");
MODULE_AUTHOR("Bhavin Sharma <bhavin.sharma@siliconsignals.io>");
MODULE_DESCRIPTION("STC3117 Fuel Gauge Driver");
