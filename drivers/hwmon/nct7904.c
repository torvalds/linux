/*
 * nct7904.c - driver for Nuvoton NCT7904D.
 *
 * Copyright (c) 2015 Kontron
 * Author: Vadim V. Vlasov <vvlasov@dev.rtsoft.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#define VENDOR_ID_REG		0x7A	/* Any bank */
#define NUVOTON_ID		0x50
#define CHIP_ID_REG		0x7B	/* Any bank */
#define NCT7904_ID		0xC5
#define DEVICE_ID_REG		0x7C	/* Any bank */

#define BANK_SEL_REG		0xFF
#define BANK_0			0x00
#define BANK_1			0x01
#define BANK_2			0x02
#define BANK_3			0x03
#define BANK_4			0x04
#define BANK_MAX		0x04

#define FANIN_MAX		12	/* Counted from 1 */
#define VSEN_MAX		21	/* VSEN1..14, 3VDD, VBAT, V3VSB,
					   LTD (not a voltage), VSEN17..19 */
#define FANCTL_MAX		4	/* Counted from 1 */
#define TCPU_MAX		8	/* Counted from 1 */
#define TEMP_MAX		4	/* Counted from 1 */

#define VT_ADC_CTRL0_REG	0x20	/* Bank 0 */
#define VT_ADC_CTRL1_REG	0x21	/* Bank 0 */
#define VT_ADC_CTRL2_REG	0x22	/* Bank 0 */
#define FANIN_CTRL0_REG		0x24
#define FANIN_CTRL1_REG		0x25
#define DTS_T_CTRL0_REG		0x26
#define DTS_T_CTRL1_REG		0x27
#define VT_ADC_MD_REG		0x2E

#define VSEN1_HV_REG		0x40	/* Bank 0; 2 regs (HV/LV) per sensor */
#define TEMP_CH1_HV_REG		0x42	/* Bank 0; same as VSEN2_HV */
#define LTD_HV_REG		0x62	/* Bank 0; 2 regs in VSEN range */
#define FANIN1_HV_REG		0x80	/* Bank 0; 2 regs (HV/LV) per sensor */
#define T_CPU1_HV_REG		0xA0	/* Bank 0; 2 regs (HV/LV) per sensor */

#define PRTS_REG		0x03	/* Bank 2 */
#define FANCTL1_FMR_REG		0x00	/* Bank 3; 1 reg per channel */
#define FANCTL1_OUT_REG		0x10	/* Bank 3; 1 reg per channel */

static const unsigned short normal_i2c[] = {
	0x2d, 0x2e, I2C_CLIENT_END
};

struct nct7904_data {
	struct i2c_client *client;
	struct mutex bank_lock;
	int bank_sel;
	u32 fanin_mask;
	u32 vsen_mask;
	u32 tcpu_mask;
	u8 fan_mode[FANCTL_MAX];
};

/* Access functions */
static int nct7904_bank_lock(struct nct7904_data *data, unsigned bank)
{
	int ret;

	mutex_lock(&data->bank_lock);
	if (data->bank_sel == bank)
		return 0;
	ret = i2c_smbus_write_byte_data(data->client, BANK_SEL_REG, bank);
	if (ret == 0)
		data->bank_sel = bank;
	else
		data->bank_sel = -1;
	return ret;
}

static inline void nct7904_bank_release(struct nct7904_data *data)
{
	mutex_unlock(&data->bank_lock);
}

/* Read 1-byte register. Returns unsigned reg or -ERRNO on error. */
static int nct7904_read_reg(struct nct7904_data *data,
			    unsigned bank, unsigned reg)
{
	struct i2c_client *client = data->client;
	int ret;

	ret = nct7904_bank_lock(data, bank);
	if (ret == 0)
		ret = i2c_smbus_read_byte_data(client, reg);

	nct7904_bank_release(data);
	return ret;
}

/*
 * Read 2-byte register. Returns register in big-endian format or
 * -ERRNO on error.
 */
static int nct7904_read_reg16(struct nct7904_data *data,
			      unsigned bank, unsigned reg)
{
	struct i2c_client *client = data->client;
	int ret, hi;

	ret = nct7904_bank_lock(data, bank);
	if (ret == 0) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret >= 0) {
			hi = ret;
			ret = i2c_smbus_read_byte_data(client, reg + 1);
			if (ret >= 0)
				ret |= hi << 8;
		}
	}

	nct7904_bank_release(data);
	return ret;
}

/* Write 1-byte register. Returns 0 or -ERRNO on error. */
static int nct7904_write_reg(struct nct7904_data *data,
			     unsigned bank, unsigned reg, u8 val)
{
	struct i2c_client *client = data->client;
	int ret;

	ret = nct7904_bank_lock(data, bank);
	if (ret == 0)
		ret = i2c_smbus_write_byte_data(client, reg, val);

	nct7904_bank_release(data);
	return ret;
}

/* FANIN ATTR */
static ssize_t show_fan(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret;
	unsigned cnt, rpm;

	ret = nct7904_read_reg16(data, BANK_0, FANIN1_HV_REG + index * 2);
	if (ret < 0)
		return ret;
	cnt = ((ret & 0xff00) >> 3) | (ret & 0x1f);
	if (cnt == 0x1fff)
		rpm = 0;
	else
		rpm = 1350000 / cnt;
	return sprintf(buf, "%u\n", rpm);
}

static umode_t nct7904_fanin_is_visible(struct kobject *kobj,
					struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nct7904_data *data = dev_get_drvdata(dev);

	if (data->fanin_mask & (1 << n))
		return a->mode;
	return 0;
}

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, show_fan, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_input, S_IRUGO, show_fan, NULL, 3);
static SENSOR_DEVICE_ATTR(fan5_input, S_IRUGO, show_fan, NULL, 4);
static SENSOR_DEVICE_ATTR(fan6_input, S_IRUGO, show_fan, NULL, 5);
static SENSOR_DEVICE_ATTR(fan7_input, S_IRUGO, show_fan, NULL, 6);
static SENSOR_DEVICE_ATTR(fan8_input, S_IRUGO, show_fan, NULL, 7);
static SENSOR_DEVICE_ATTR(fan9_input, S_IRUGO, show_fan, NULL, 8);
static SENSOR_DEVICE_ATTR(fan10_input, S_IRUGO, show_fan, NULL, 9);
static SENSOR_DEVICE_ATTR(fan11_input, S_IRUGO, show_fan, NULL, 10);
static SENSOR_DEVICE_ATTR(fan12_input, S_IRUGO, show_fan, NULL, 11);

static struct attribute *nct7904_fanin_attrs[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan5_input.dev_attr.attr,
	&sensor_dev_attr_fan6_input.dev_attr.attr,
	&sensor_dev_attr_fan7_input.dev_attr.attr,
	&sensor_dev_attr_fan8_input.dev_attr.attr,
	&sensor_dev_attr_fan9_input.dev_attr.attr,
	&sensor_dev_attr_fan10_input.dev_attr.attr,
	&sensor_dev_attr_fan11_input.dev_attr.attr,
	&sensor_dev_attr_fan12_input.dev_attr.attr,
	NULL
};

static const struct attribute_group nct7904_fanin_group = {
	.attrs = nct7904_fanin_attrs,
	.is_visible = nct7904_fanin_is_visible,
};

/* VSEN ATTR */
static ssize_t show_voltage(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret;
	int volt;

	ret = nct7904_read_reg16(data, BANK_0, VSEN1_HV_REG + index * 2);
	if (ret < 0)
		return ret;
	volt = ((ret & 0xff00) >> 5) | (ret & 0x7);
	if (index < 14)
		volt *= 2; /* 0.002V scale */
	else
		volt *= 6; /* 0.006V scale */

	return sprintf(buf, "%d\n", volt);
}

static ssize_t show_ltemp(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret;
	int temp;

	ret = nct7904_read_reg16(data, BANK_0, LTD_HV_REG);
	if (ret < 0)
		return ret;
	temp = ((ret & 0xff00) >> 5) | (ret & 0x7);
	temp = sign_extend32(temp, 10) * 125;

	return sprintf(buf, "%d\n", temp);
}

static umode_t nct7904_vsen_is_visible(struct kobject *kobj,
				       struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nct7904_data *data = dev_get_drvdata(dev);

	if (data->vsen_mask & (1 << n))
		return a->mode;
	return 0;
}

static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, show_voltage, NULL, 0);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, show_voltage, NULL, 1);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, show_voltage, NULL, 2);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, show_voltage, NULL, 3);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, show_voltage, NULL, 4);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, show_voltage, NULL, 5);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, show_voltage, NULL, 6);
static SENSOR_DEVICE_ATTR(in8_input, S_IRUGO, show_voltage, NULL, 7);
static SENSOR_DEVICE_ATTR(in9_input, S_IRUGO, show_voltage, NULL, 8);
static SENSOR_DEVICE_ATTR(in10_input, S_IRUGO, show_voltage, NULL, 9);
static SENSOR_DEVICE_ATTR(in11_input, S_IRUGO, show_voltage, NULL, 10);
static SENSOR_DEVICE_ATTR(in12_input, S_IRUGO, show_voltage, NULL, 11);
static SENSOR_DEVICE_ATTR(in13_input, S_IRUGO, show_voltage, NULL, 12);
static SENSOR_DEVICE_ATTR(in14_input, S_IRUGO, show_voltage, NULL, 13);
/*
 * Next 3 voltage sensors have specific names in the Nuvoton doc
 * (3VDD, VBAT, 3VSB) but we use vacant numbers for them.
 */
static SENSOR_DEVICE_ATTR(in15_input, S_IRUGO, show_voltage, NULL, 14);
static SENSOR_DEVICE_ATTR(in16_input, S_IRUGO, show_voltage, NULL, 15);
static SENSOR_DEVICE_ATTR(in20_input, S_IRUGO, show_voltage, NULL, 16);
/* This is not a voltage, but a local temperature sensor. */
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_ltemp, NULL, 0);
static SENSOR_DEVICE_ATTR(in17_input, S_IRUGO, show_voltage, NULL, 18);
static SENSOR_DEVICE_ATTR(in18_input, S_IRUGO, show_voltage, NULL, 19);
static SENSOR_DEVICE_ATTR(in19_input, S_IRUGO, show_voltage, NULL, 20);

static struct attribute *nct7904_vsen_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	&sensor_dev_attr_in12_input.dev_attr.attr,
	&sensor_dev_attr_in13_input.dev_attr.attr,
	&sensor_dev_attr_in14_input.dev_attr.attr,
	&sensor_dev_attr_in15_input.dev_attr.attr,
	&sensor_dev_attr_in16_input.dev_attr.attr,
	&sensor_dev_attr_in20_input.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_in17_input.dev_attr.attr,
	&sensor_dev_attr_in18_input.dev_attr.attr,
	&sensor_dev_attr_in19_input.dev_attr.attr,
	NULL
};

static const struct attribute_group nct7904_vsen_group = {
	.attrs = nct7904_vsen_attrs,
	.is_visible = nct7904_vsen_is_visible,
};

/* CPU_TEMP ATTR */
static ssize_t show_tcpu(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret;
	int temp;

	ret = nct7904_read_reg16(data, BANK_0, T_CPU1_HV_REG + index * 2);
	if (ret < 0)
		return ret;

	temp = ((ret & 0xff00) >> 5) | (ret & 0x7);
	temp = sign_extend32(temp, 10) * 125;
	return sprintf(buf, "%d\n", temp);
}

static umode_t nct7904_tcpu_is_visible(struct kobject *kobj,
				       struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nct7904_data *data = dev_get_drvdata(dev);

	if (data->tcpu_mask & (1 << n))
		return a->mode;
	return 0;
}

/* "temp1_input" reserved for local temp */
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_tcpu, NULL, 0);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_tcpu, NULL, 1);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_tcpu, NULL, 2);
static SENSOR_DEVICE_ATTR(temp5_input, S_IRUGO, show_tcpu, NULL, 3);
static SENSOR_DEVICE_ATTR(temp6_input, S_IRUGO, show_tcpu, NULL, 4);
static SENSOR_DEVICE_ATTR(temp7_input, S_IRUGO, show_tcpu, NULL, 5);
static SENSOR_DEVICE_ATTR(temp8_input, S_IRUGO, show_tcpu, NULL, 6);
static SENSOR_DEVICE_ATTR(temp9_input, S_IRUGO, show_tcpu, NULL, 7);

static struct attribute *nct7904_tcpu_attrs[] = {
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp6_input.dev_attr.attr,
	&sensor_dev_attr_temp7_input.dev_attr.attr,
	&sensor_dev_attr_temp8_input.dev_attr.attr,
	&sensor_dev_attr_temp9_input.dev_attr.attr,
	NULL
};

static const struct attribute_group nct7904_tcpu_group = {
	.attrs = nct7904_tcpu_attrs,
	.is_visible = nct7904_tcpu_is_visible,
};

/* PWM ATTR */
static ssize_t store_pwm(struct device *dev, struct device_attribute *devattr,
			 const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct nct7904_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;
	if (val > 255)
		return -EINVAL;

	ret = nct7904_write_reg(data, BANK_3, FANCTL1_OUT_REG + index, val);

	return ret ? ret : count;
}

static ssize_t show_pwm(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct nct7904_data *data = dev_get_drvdata(dev);
	int val;

	val = nct7904_read_reg(data, BANK_3, FANCTL1_OUT_REG + index);
	if (val < 0)
		return val;

	return sprintf(buf, "%d\n", val);
}

static ssize_t store_enable(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct nct7904_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;
	if (val < 1 || val > 2 || (val == 2 && !data->fan_mode[index]))
		return -EINVAL;

	ret = nct7904_write_reg(data, BANK_3, FANCTL1_FMR_REG + index,
				val == 2 ? data->fan_mode[index] : 0);

	return ret ? ret : count;
}

/* Return 1 for manual mode or 2 for SmartFan mode */
static ssize_t show_enable(struct device *dev,
			   struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct nct7904_data *data = dev_get_drvdata(dev);
	int val;

	val = nct7904_read_reg(data, BANK_3, FANCTL1_FMR_REG + index);
	if (val < 0)
		return val;

	return sprintf(buf, "%d\n", val ? 2 : 1);
}

/* 2 attributes per channel: pwm and mode */
static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR,
			show_pwm, store_pwm, 0);
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR,
			show_enable, store_enable, 0);
static SENSOR_DEVICE_ATTR(pwm2, S_IRUGO | S_IWUSR,
			show_pwm, store_pwm, 1);
static SENSOR_DEVICE_ATTR(pwm2_enable, S_IRUGO | S_IWUSR,
			show_enable, store_enable, 1);
static SENSOR_DEVICE_ATTR(pwm3, S_IRUGO | S_IWUSR,
			show_pwm, store_pwm, 2);
static SENSOR_DEVICE_ATTR(pwm3_enable, S_IRUGO | S_IWUSR,
			show_enable, store_enable, 2);
static SENSOR_DEVICE_ATTR(pwm4, S_IRUGO | S_IWUSR,
			show_pwm, store_pwm, 3);
static SENSOR_DEVICE_ATTR(pwm4_enable, S_IRUGO | S_IWUSR,
			show_enable, store_enable, 3);

static struct attribute *nct7904_fanctl_attrs[] = {
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm4.dev_attr.attr,
	&sensor_dev_attr_pwm4_enable.dev_attr.attr,
	NULL
};

static const struct attribute_group nct7904_fanctl_group = {
	.attrs = nct7904_fanctl_attrs,
};

static const struct attribute_group *nct7904_groups[] = {
	&nct7904_fanin_group,
	&nct7904_vsen_group,
	&nct7904_tcpu_group,
	&nct7904_fanctl_group,
	NULL
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int nct7904_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if (!i2c_check_functionality(adapter,
				     I2C_FUNC_SMBUS_READ_BYTE |
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	/* Determine the chip type. */
	if (i2c_smbus_read_byte_data(client, VENDOR_ID_REG) != NUVOTON_ID ||
	    i2c_smbus_read_byte_data(client, CHIP_ID_REG) != NCT7904_ID ||
	    (i2c_smbus_read_byte_data(client, DEVICE_ID_REG) & 0xf0) != 0x50 ||
	    (i2c_smbus_read_byte_data(client, BANK_SEL_REG) & 0xf8) != 0x00)
		return -ENODEV;

	strlcpy(info->type, "nct7904", I2C_NAME_SIZE);

	return 0;
}

static int nct7904_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct nct7904_data *data;
	struct device *hwmon_dev;
	struct device *dev = &client->dev;
	int ret, i;
	u32 mask;

	data = devm_kzalloc(dev, sizeof(struct nct7904_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->bank_lock);
	data->bank_sel = -1;

	/* Setup sensor groups. */
	/* FANIN attributes */
	ret = nct7904_read_reg16(data, BANK_0, FANIN_CTRL0_REG);
	if (ret < 0)
		return ret;
	data->fanin_mask = (ret >> 8) | ((ret & 0xff) << 8);

	/*
	 * VSEN attributes
	 *
	 * Note: voltage sensors overlap with external temperature
	 * sensors. So, if we ever decide to support the latter
	 * we will have to adjust 'vsen_mask' accordingly.
	 */
	mask = 0;
	ret = nct7904_read_reg16(data, BANK_0, VT_ADC_CTRL0_REG);
	if (ret >= 0)
		mask = (ret >> 8) | ((ret & 0xff) << 8);
	ret = nct7904_read_reg(data, BANK_0, VT_ADC_CTRL2_REG);
	if (ret >= 0)
		mask |= (ret << 16);
	data->vsen_mask = mask;

	/* CPU_TEMP attributes */
	ret = nct7904_read_reg16(data, BANK_0, DTS_T_CTRL0_REG);
	if (ret < 0)
		return ret;
	data->tcpu_mask = ((ret >> 8) & 0xf) | ((ret & 0xf) << 4);

	for (i = 0; i < FANCTL_MAX; i++) {
		ret = nct7904_read_reg(data, BANK_3, FANCTL1_FMR_REG + i);
		if (ret < 0)
			return ret;
		data->fan_mode[i] = ret;
	}

	hwmon_dev =
		devm_hwmon_device_register_with_groups(dev, client->name, data,
						       nct7904_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id nct7904_id[] = {
	{"nct7904", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, nct7904_id);

static struct i2c_driver nct7904_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "nct7904",
	},
	.probe = nct7904_probe,
	.id_table = nct7904_id,
	.detect = nct7904_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(nct7904_driver);

MODULE_AUTHOR("Vadim V. Vlasov <vvlasov@dev.rtsoft.ru>");
MODULE_DESCRIPTION("Hwmon driver for NUVOTON NCT7904");
MODULE_LICENSE("GPL");
