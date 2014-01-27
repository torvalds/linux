/*
 * drivers/power/axp_power/axp20-mfd.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "axp-rw.h"

#ifdef CONFIG_AXP_HWMON

#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/err.h>

static struct axp_mfd_chip *axp20_update_device(struct device *dev);

static ssize_t
show_temp(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct axp_mfd_chip *data = axp20_update_device(dev);
	if (attr->index == 1)
		return sprintf(buf, "264800\n");
	if (attr->index == 2)
		return sprintf(buf, "-144700\n");
	return sprintf(buf, "%d\n", data->temperature * 100);
}


static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_min, S_IRUGO, show_temp, NULL, 2);

static struct attribute *axp20_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	NULL
};

static const struct attribute_group axp20_group = {
	.attrs = axp20_attributes,
};


/*
 *  * function that update the status of the chips (temperature)
 *   */
static struct axp_mfd_chip *axp20_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct axp_mfd_chip *data = i2c_get_clientdata(client);
	int err;
	u8 high, low;

	mutex_lock(&data->lock);

	if (time_after(jiffies, data->last_updated + HZ * 2)
		|| !data->valid) {
		dev_dbg(&client->dev, "Updating axp20 data\n");
		/* AXP202 datasheet page 25, 0x000 means -144.7,
		 * 0xfff means 264.8, 4096 steps of 0.1 degress */
		err = __axp_read(client, 0x5E, &high);
		if (err) {
			dev_err(dev, "AXP Error while reading high\n");
			high = 0;
		}

		err = __axp_read(client, 0x5F, &low);
		if (err) {
			dev_err(dev, "AXP Error while reading low\n");
			low = 0;
		}

		data->temperature = -1447 + ((high << 4) + (low && 0x0F));
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->lock);
	return data;
}

#endif


static int __devinit axp20_init_chip(struct axp_mfd_chip *chip)
{
	uint8_t chip_id;
	uint8_t v[19] = { /* POWER20_INTEN1 */ 0x00,
		POWER20_INTEN2,  0x00, POWER20_INTEN3,  0x00,
		POWER20_INTEN4,  0x00, POWER20_INTEN5,  0x00,
		POWER20_INTSTS1, 0xff, POWER20_INTSTS2, 0xff,
		POWER20_INTSTS3, 0xff, POWER20_INTSTS4, 0xff,
		POWER20_INTSTS5, 0xff };
	int err;
#ifdef CONFIG_AXP_HWMON
	u8 enabled;
#endif
	/*read chip id*/
	err =  __axp_read(chip->client, POWER20_IC_TYPE, &chip_id);
	if (err) {
	    printk("[AXP20-MFD] try to read chip id failed!\n");
		return err;
	}

	/* Mask and clear all IRQs */
	err =  __axp_writes(chip->client, POWER20_INTEN1, 19, v);
	if (err) {
	    printk("[AXP20-MFD] try to clear irq failed!\n");
		return err;
	}
	chip->irqs_enabled = 0;

	dev_info(chip->dev, "AXP (CHIP ID: 0x%02x) detected\n", chip_id);
	chip->type = AXP20;

#ifdef CONFIG_AXP_HWMON
	err = __axp_read(chip->client, 0x83, &enabled);
	if (err) {
		dev_info(chip->dev, "AXP Cannot get internal temperature monitoring status\n");
		return err;
	}
	if ((enabled & 0x80) > 0) {
		chip->itm_enabled = 1;
		dev_info(chip->dev, "AXP internal temperature monitoring enabled\n");

		/* Register sysfs hooks */
		err = sysfs_create_group(&chip->client->dev.kobj, &axp20_group);
		if (err)
			return err;

		chip->hwmon_dev = hwmon_device_register(&chip->client->dev);
		if (IS_ERR(chip->hwmon_dev)) {
			err = PTR_ERR(chip->hwmon_dev);
			goto exit_remove_files;
		}
	} else {
		dev_info(chip->dev, "AXP internal temperature monitoring disabled\n");
		/* TODO enable it ?*/
		chip->itm_enabled = 0;
	}
#endif

	return 0;
#ifdef CONFIG_AXP_HWMON
exit_remove_files:
	sysfs_remove_group(&chip->client->dev.kobj, &axp20_group);
	return err;
#endif
}

static int axp20_disable_irqs(struct axp_mfd_chip *chip, uint64_t irqs)
{
	uint8_t v[9];
	int ret;

	chip->irqs_enabled &= ~irqs;

	v[0] = ((chip->irqs_enabled) & 0xff);
	v[1] = POWER20_INTEN2;
	v[2] = ((chip->irqs_enabled) >> 8) & 0xff;
	v[3] = POWER20_INTEN3;
	v[4] = ((chip->irqs_enabled) >> 16) & 0xff;
	v[5] = POWER20_INTEN4;
	v[6] = ((chip->irqs_enabled) >> 24) & 0xff;
	v[7] = POWER20_INTEN5;
	v[8] = ((chip->irqs_enabled) >> 32) & 0xff;
	ret =  __axp_writes(chip->client, POWER20_INTEN1, 9, v);

	return ret;

}

static int axp20_enable_irqs(struct axp_mfd_chip *chip, uint64_t irqs)
{
	uint8_t v[9];
	int ret;

	chip->irqs_enabled |=  irqs;

	v[0] = ((chip->irqs_enabled) & 0xff);
	v[1] = POWER20_INTEN2;
	v[2] = ((chip->irqs_enabled) >> 8) & 0xff;
	v[3] = POWER20_INTEN3;
	v[4] = ((chip->irqs_enabled) >> 16) & 0xff;
	v[5] = POWER20_INTEN4;
	v[6] = ((chip->irqs_enabled) >> 24) & 0xff;
	v[7] = POWER20_INTEN5;
	v[8] = ((chip->irqs_enabled) >> 32) & 0xff;
	ret =  __axp_writes(chip->client, POWER20_INTEN1, 9, v);

	return ret;
}

static int axp20_read_irqs(struct axp_mfd_chip *chip, uint64_t *irqs)
{
	uint8_t v[5] = {0, 0, 0, 0, 0};
	int ret;
	ret =  __axp_reads(chip->client, POWER20_INTSTS1, 5, v);
	if (ret < 0)
		return ret;

	*irqs =(((uint64_t) v[4]) << 32) |(((uint64_t) v[3]) << 24) | (((uint64_t) v[2])<< 16) | (((uint64_t)v[1]) << 8) | ((uint64_t) v[0]);
	return 0;
}


static ssize_t axp20_offvol_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val = 0;
	axp_read(dev,POWER20_VOFF_SET,&val);
	return sprintf(buf,"%d\n",(val & 0x07) * 100 + 2600);
}

static ssize_t axp20_offvol_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if (tmp < 2600)
		tmp = 2600;
	if (tmp > 3300)
		tmp = 3300;

	axp_read(dev,POWER20_VOFF_SET,&val);
	val &= 0xf8;
	val |= ((tmp - 2600) / 100);
	axp_write(dev,POWER20_VOFF_SET,val);
	return count;
}

static ssize_t axp20_noedelay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER20_OFF_CTL,&val);
	if( (val & 0x03) == 0)
		return sprintf(buf,"%d\n",128);
	else
		return sprintf(buf,"%d\n",(val & 0x03) * 1000);
}

static ssize_t axp20_noedelay_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if (tmp < 1000)
		tmp = 128;
	if (tmp > 3000)
		tmp = 3000;
	axp_read(dev,POWER19_OFF_CTL,&val);
	val &= 0xfc;
	val |= ((tmp) / 1000);
	axp_write(dev,POWER20_OFF_CTL,val);
	return count;
}

static ssize_t axp20_pekopen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	int tmp = 0;
	axp_read(dev,POWER20_PEK_SET,&val);
	switch(val >> 6){
		case 0: tmp = 128;break;
		case 1: tmp = 3000;break;
		case 2: tmp = 1000;break;
		case 3: tmp = 2000;break;
		default:
			tmp = 0;break;
	}
	return sprintf(buf,"%d\n",tmp);
}

static ssize_t axp20_pekopen_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	axp_read(dev,POWER20_PEK_SET,&val);
	if (tmp < 1000)
		val &= 0x3f;
	else if(tmp < 2000){
		val &= 0x3f;
		val |= 0x80;
	}
	else if(tmp < 3000){
		val &= 0x3f;
		val |= 0xc0;
	}
	else {
		val &= 0x3f;
		val |= 0x40;
	}
	axp_write(dev,POWER20_PEK_SET,val);
	return count;
}

static ssize_t axp20_peklong_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val = 0;
	axp_read(dev,POWER20_PEK_SET,&val);
	return sprintf(buf,"%d\n",((val >> 4) & 0x03) * 500 + 1000);
}

static ssize_t axp20_peklong_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp < 1000)
		tmp = 1000;
	if(tmp > 2500)
		tmp = 2500;
	axp_read(dev,POWER20_PEK_SET,&val);
	val &= 0xcf;
	val |= (((tmp - 1000) / 500) << 4);
	axp_write(dev,POWER20_PEK_SET,val);
	return count;
}

static ssize_t axp20_peken_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER20_PEK_SET,&val);
	return sprintf(buf,"%d\n",((val >> 3) & 0x01));
}

static ssize_t axp20_peken_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp)
		tmp = 1;
	axp_read(dev,POWER20_PEK_SET,&val);
	val &= 0xf7;
	val |= (tmp << 3);
	axp_write(dev,POWER20_PEK_SET,val);
	return count;
}

static ssize_t axp20_pekdelay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER20_PEK_SET,&val);

	return sprintf(buf,"%d\n",((val >> 2) & 0x01)? 64:8);
}

static ssize_t axp20_pekdelay_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp <= 8)
		tmp = 0;
	else
		tmp = 1;
	axp_read(dev,POWER20_PEK_SET,&val);
	val &= 0xfb;
	val |= tmp << 2;
	axp_write(dev,POWER20_PEK_SET,val);
	return count;
}

static ssize_t axp20_pekclose_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER20_PEK_SET,&val);
	return sprintf(buf,"%d\n",((val & 0x03) * 2000) + 4000);
}

static ssize_t axp20_pekclose_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp < 4000)
		tmp = 4000;
	if(tmp > 10000)
		tmp =10000;
	tmp = (tmp - 4000) / 2000 ;
	axp_read(dev,POWER20_PEK_SET,&val);
	val &= 0xfc;
	val |= tmp ;
	axp_write(dev,POWER20_PEK_SET,val);
	return count;
}

static ssize_t axp20_ovtemclsen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER20_HOTOVER_CTL,&val);
	return sprintf(buf,"%d\n",((val >> 2) & 0x01));
}

static ssize_t axp20_ovtemclsen_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp)
		tmp = 1;
	axp_read(dev,POWER20_HOTOVER_CTL,&val);
	val &= 0xfb;
	val |= tmp << 2 ;
	axp_write(dev,POWER20_HOTOVER_CTL,val);
	return count;
}

static ssize_t axp20_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,axp_reg_addr,&val);
	return sprintf(buf,"REG[%x]=%x\n",axp_reg_addr,val);
}

static ssize_t axp20_reg_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 16);
	if( tmp < 256 )
		axp_reg_addr = tmp;
	else {
		val = tmp & 0x00FF;
		axp_reg_addr= (tmp >> 8) & 0x00FF;
		axp_write(dev,axp_reg_addr, val);
	}
	return count;
}

static ssize_t axp20_regs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
  uint8_t val[2];
	axp_reads(dev,axp_reg_addr,2,val);
	return sprintf(buf,"REG[0x%x]=0x%x,REG[0x%x]=0x%x\n",axp_reg_addr,val[0],axp_reg_addr+1,val[1]);
}

static ssize_t axp20_regs_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val[3];
	tmp = simple_strtoul(buf, NULL, 16);
	if( tmp < 256 )
		axp_reg_addr = tmp;
	else {
		axp_reg_addr= (tmp >> 16) & 0xFF;
		val[0] = (tmp >> 8) & 0xFF;
		val[1] = axp_reg_addr + 1;
		val[2] = tmp & 0xFF;
		axp_writes(dev,axp_reg_addr,3,val);
	}
	return count;
}

static struct device_attribute axp20_mfd_attrs[] = {
	AXP_MFD_ATTR(axp20_offvol),
	AXP_MFD_ATTR(axp20_noedelay),
	AXP_MFD_ATTR(axp20_pekopen),
	AXP_MFD_ATTR(axp20_peklong),
	AXP_MFD_ATTR(axp20_peken),
	AXP_MFD_ATTR(axp20_pekdelay),
	AXP_MFD_ATTR(axp20_pekclose),
	AXP_MFD_ATTR(axp20_ovtemclsen),
	AXP_MFD_ATTR(axp20_reg),
	AXP_MFD_ATTR(axp20_regs),
};
