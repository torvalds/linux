// SPDX-License-Identifier: GPL-2.0
/*
 * ELAN HID-I2C TouchScreen driver.
 *
 * Copyright (C) 2014 Elan Microelectronics Corporation.
 *
 * Author: Chuming Zhang <chuming.zhang@elanic.com.cn>
 */

#include "elan_ts.h"
#include <linux/of_gpio.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#define IntToASCII(c)   (c)>9?((c)+0x37):((c)+0x30)

static ssize_t store_disable_irq(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
/*	
	if (ts->irq_lock_flag == 0) {
		disable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 1;
	}
*/
	elan_switch_irq(ts, 0);
	dev_info(&client->dev, "Disable IRQ.\n");
	return count;
}
static DEVICE_ATTR(disable_irq, S_IWUSR, NULL, store_disable_irq);

static ssize_t store_enable_irq(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	/*
	if (ts->irq_lock_flag == 1) {
		enable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 0;
	}
	*/
	elan_switch_irq(ts, 1);
	dev_info(&client->dev, "Enable IRQ.\n");
	return count;
}
static DEVICE_ATTR(enable_irq, S_IWUSR, NULL, store_enable_irq);


static ssize_t store_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	
	elan_ts_hw_reset(&ts->hw_info);
	dev_info(&client->dev,
			"Reset Touch Screen Controller!\n");
	return count;
}
static DEVICE_ATTR(reset, S_IWUSR, NULL, store_reset);

static ssize_t show_gpio_int(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	
	return sprintf(buf, "%d\n",
			gpio_get_value(ts->hw_info.intr_gpio));
}
static DEVICE_ATTR(gpio_int, S_IRUGO, show_gpio_int, NULL);

static ssize_t store_calibrate(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);	
	int ret = 0;
/*
	if (ts->irq_lock_flag == 0) {
		disable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 1;
	}
*/
	elan_switch_irq(ts, 0);
	ret = elan_ts_calibrate(client);
	
	if (ret == 0)
		dev_info(&client->dev, "ELAN CALIBRATE Success\n");
	else
		dev_err(&client->dev, "ELAN CALIBRATE Fail\n");
/*	
	if (ts->irq_lock_flag == 1) {
		enable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 0;
	}
*/
	elan_switch_irq(ts, 1);
	return count;
}
static DEVICE_ATTR(calibrate, S_IWUSR, NULL, store_calibrate);


static ssize_t store_check_rek(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	int ret = 0;

/*	
	if (ts->irq_lock_flag == 0) {
		disable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 1;
	}
*/
	elan_switch_irq(ts, 0);
	ret = elan_ts_check_calibrate(client);
	
	if (ret)
		dev_err(&client->dev, "ELAN CALIBRATE CHECK Fail\n");
	else
		dev_info(&client->dev, "ELAN CHECK CALIBRATE Success\n");
/*	
	if (ts->irq_lock_flag == 1) {
		enable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 0;
	}
*/
	elan_switch_irq(ts, 1);
	return count;
}
static DEVICE_ATTR(check_rek, S_IWUSR, NULL, store_check_rek);

static ssize_t show_fw_info(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	
	return sprintf(buf, "FW VER = 0x%04x, FW ID = 0x%04x, BC VER = 0x%04x, tx:rx = %d:%d\n",\
			ts->fw_info.fw_ver, ts->fw_info.fw_id, ts->fw_info.fw_bcv, ts->fw_info.tx, ts->fw_info.rx);
}
static ssize_t store_fw_info(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
/*
	if (ts->irq_lock_flag == 0) {
		disable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 1;
	}
*/
	elan_switch_irq(ts, 0);

	elan__fw_packet_handler(ts->client);
/*
	if (ts->irq_lock_flag == 1) {
		enable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 0;
	}
*/
	elan_switch_irq(ts, 1);

	return count;
}
static DEVICE_ATTR(fw_info, S_IWUSR | S_IRUSR, show_fw_info, store_fw_info);

static ssize_t store_iap_status(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
/*
	if (ts->irq_lock_flag == 0) {
		disable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 1;
	}
*/
	elan_switch_irq(ts, 0);
	elan_ts_hw_reset(&ts->hw_info);
	mdelay(200);
	elan__hello_packet_handler(client, ts->chip_type);

/*	
	if (ts->irq_lock_flag == 1) {
		enable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 0;
	}
*/
	elan_switch_irq(ts, 1);
	return count;
}

static ssize_t show_iap_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	
	return sprintf(buf, "IAP STATUS = %s\n",(ts->recover < 0 ? "UNKNOW":(ts->recover == 0x01 ? "RECOVERY":"NORMAL")));
}
static DEVICE_ATTR(iap_status, S_IWUSR | S_IRUSR, show_iap_status, store_iap_status);

static ssize_t store_fw_upgrade(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
/*
	if (ts->irq_lock_flag == 0) {
		disable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 1;
	}
*/
	elan_switch_irq(ts, 0);
//	elan_get_vendor_fw(ts,ts->fw_store_type);
	elan_FW_Update(ts->client);
/*
	if (ts->irq_lock_flag == 1) {
		enable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 0;
	}
*/
	elan_switch_irq(ts, 1);
	return count;	
}
static DEVICE_ATTR(fw_update, S_IWUSR, NULL,  store_fw_upgrade);


static ssize_t show_fw_store(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	
	if (ts->fw_store_type > -1 && ts->fw_store_type < 3) {
		return sprintf(buf, "FW STORE = %s\n",\
				(ts->fw_store_type == FROM_SYS_ETC_FIRMWARE ? "/system/etc/firmware/elants_i2c.ekt":\
				(ts->fw_store_type == FROM_SDCARD_FIRMWARE ? "/data/local/tmp/elants_i2c.ekt":"build in driver")));
	} else {
		return  sprintf(buf, "FW STORE TYPE OUT OF RANGE\n");
	}
}


static ssize_t store_fw_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{

	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	int type;

	sscanf(buf,"%d",&type);
	if (type > -1 && type < 3) {	
		ts->fw_store_type = type;
	} else {
		dev_info(&client->dev, "[elan] fw store type out of range!!!\n");
	}
	return count;

}
static DEVICE_ATTR(fw_store, S_IWUSR | S_IRUSR, show_fw_store, store_fw_store);

#if 0
static ssize_t store_tp_module_test(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{

	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	int ret = 0;

	ret = elan_tp_module_test(ts);
	return count;
}
static DEVICE_ATTR(elan_tp_module_test, S_IWUSR, NULL,  store_tp_module_test);
#endif
static ssize_t store_tp_print_level(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int level = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	
	sscanf(buf, "%x", &level);
	ts->level = level;
	return count;
}


static ssize_t show_tp_print_level(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	
	return sprintf(buf, "PRINT LEVEL = %s\n",\
			(ts->level == TP_DEBUG ? "DEBUG":(ts->level == TP_INFO ? "INFO" : (ts->level == TP_WARNING ?"WARNING":"ERROR"))));
}
static DEVICE_ATTR(tp_print_level, S_IWUSR | S_IRUSR, show_tp_print_level, store_tp_print_level);

static ssize_t store_tp_cmd_send(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	
	int valid_size = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	char cmd[37] = {0x04, 0x00, 0x23, 0x00, 0x03};
	int ret = 0;

	sscanf(buf, "%d:%x:%x:%x:%x:%x:%x:%x:%x", &valid_size,\
		(int *)&cmd[5], (int *)&cmd[6], (int *)&cmd[7], (int *)&cmd[8],\
		(int *)&cmd[9], (int *)&cmd[10], (int *)&cmd[11], (int *)&cmd[12]);
/*
	if (ts->irq_lock_flag == 0) {
		disable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 1;
	}
*/
	elan_switch_irq(ts, 0);
	dev_info(&client->dev, "cmd: %x:%x:%x:%x:%x:%x:%x:%x\n",cmd[5],cmd[6],cmd[7],cmd[8],cmd[9],cmd[10],cmd[11],cmd[12]);
	
	
	if (ts->chip_type == HID_TYPE_PROTOCOL) {
		ret = ts->ops->send(cmd, sizeof(cmd));
		if (ret != sizeof(cmd))
			dev_err(&client->dev, "send cmd failed %d:%ld\n", ret, sizeof(cmd));
	} else {
		ret = ts->ops->send(&cmd[7], valid_size);
		if (ret != valid_size)
			dev_err(&client->dev, "send cmd failed %d:%d\n",ret, valid_size);
	}
	return count;
}

static ssize_t show_tp_cmd_recv(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	uint8_t rbuf[67] = {0x00};

	int i = 0;
	char c1;
	char out[1024];
	int shift = 0;

	while(!ts->ops->poll()) {
		ts->ops->recv(rbuf, 67);
		dev_info(&client->dev, "rbuf: %x:%x:%x:%x:%x:%x:%x:%x\n",
			 rbuf[1],rbuf[2],rbuf[3],rbuf[4],rbuf[5],rbuf[6],rbuf[7],rbuf[8]);

		for(i = 0; i < sizeof(rbuf); i++) {
			c1 = rbuf[i] & 0xF0;
			out[3 * i + 0 + shift] = IntToASCII(c1 >> 4);
				
			c1 = rbuf[i]&0x0F;
			out[3 * i + 1 + shift] = IntToASCII(c1);
				
			if((i %7 ==0) && (i !=0)) {
				out[3 * i + 2 + shift] = 0x0A;//'\n' = 0x0A;
			} else {
				out[3 * i + 2 + shift] = 0x20;//space = 0x20;
			}
		}
		printk("%s\n", out);
		sprintf(buf + strlen(buf), "%s\n", out+shift);
		shift += 3 * i;
	}
	/*	
	if (ts->irq_lock_flag == 1) {
		enable_irq(ts->hw_info.irq_num);
		ts->irq_lock_flag = 0;
	}
	*/
	elan_switch_irq(ts, 1);
	return strlen(buf);
}


static DEVICE_ATTR(raw_cmd, S_IWUSR | S_IRUSR, show_tp_cmd_recv, store_tp_cmd_send);

static struct attribute *elan_default_attributes[] = {
	&dev_attr_enable_irq.attr,
	&dev_attr_disable_irq.attr,
	&dev_attr_gpio_int.attr,
	&dev_attr_reset.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_check_rek.attr,
	&dev_attr_fw_info.attr,
	&dev_attr_iap_status.attr,
	&dev_attr_fw_update.attr,
	&dev_attr_fw_store.attr,
//	&dev_attr_tp_module_test.attr,
	&dev_attr_tp_print_level.attr,
	&dev_attr_raw_cmd.attr,
	NULL
};

static struct attribute_group elan_default_attribute_group = {
	.name = "elan_ktf",
	.attrs = elan_default_attributes,
};

int elan_sysfs_attri_file(struct elan_ts_data *ts)
{
	int err = 0;
	
	err = sysfs_create_group(&ts->client->dev.kobj, &elan_default_attribute_group);
	if ( err ) {
		dev_err(&ts->client->dev, "[elan] %s sysfs create group error\n",__func__);
	} else {
		dev_err(&ts->client->dev,"[elan] %s sysfs create group success\n",__func__);
	}

	return err;
}

void elan_sysfs_attri_file_remove(struct elan_ts_data *ts)
{
	 sysfs_remove_group(&ts->client->dev.kobj, &elan_default_attribute_group);
}



