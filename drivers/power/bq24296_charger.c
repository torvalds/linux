/*
 * BQ24296 battery driver
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/power/bq24296_charger.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>

struct bq24296_device_info *bq24296_di;
struct bq24296_board *bq24296_pdata;
static int bq24296_int = 0;
int bq24296_mode = 0;
int bq24296_chag_down ;
#if 0
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

/*
 * Common code for BQ24296 devices read
 */
static int bq24296_i2c_reg8_read(const struct i2c_client *client, const char reg, char *buf, int count, int scl_rate)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	char reg_buf = reg;
	
	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 1;
	msgs[0].buf = &reg_buf;
	msgs[0].scl_rate = scl_rate;
//	msgs[0].udelay = client->udelay;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = count;
	msgs[1].buf = (char *)buf;
	msgs[1].scl_rate = scl_rate;
//	msgs[1].udelay = client->udelay;

	ret = i2c_transfer(adap, msgs, 2);

	return (ret == 2)? count : ret;
}
EXPORT_SYMBOL(bq24296_i2c_reg8_read);

static int bq24296_i2c_reg8_write(const struct i2c_client *client, const char reg, const char *buf, int count, int scl_rate)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	int ret;
	char *tx_buf = (char *)kmalloc(count + 1, GFP_KERNEL);
	if(!tx_buf)
		return -ENOMEM;
	tx_buf[0] = reg;
	memcpy(tx_buf+1, buf, count); 

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = count + 1;
	msg.buf = (char *)tx_buf;
	msg.scl_rate = scl_rate;
//	msg.udelay = client->udelay;

	ret = i2c_transfer(adap, &msg, 1);
	kfree(tx_buf);
	return (ret == 1) ? count : ret;

}
EXPORT_SYMBOL(bq24296_i2c_reg8_write);

static int bq24296_read(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret;
	ret = bq24296_i2c_reg8_read(client, reg, buf, len, BQ24296_SPEED);
	return ret; 
}

static int bq24296_write(struct i2c_client *client, u8 reg, u8 const buf[], unsigned len)
{
	int ret; 
	ret = bq24296_i2c_reg8_write(client, reg, buf, (int)len, BQ24296_SPEED);
	return ret;
}

static ssize_t bat_param_read(struct device *dev,struct device_attribute *attr, char *buf)
{
	int i;
	u8 buffer;
	struct bq24296_device_info *di=bq24296_di;

	for(i=0;i<11;i++)
	{
		bq24296_read(di->client,i,&buffer,1);
		DBG("reg %d value %x\n",i,buffer);		
	}
	return 0;
}
DEVICE_ATTR(battparam, 0664, bat_param_read,NULL);

static int bq24296_update_reg(struct i2c_client *client, int reg, u8 value, u8 mask )
{
	int ret =0;
	u8 retval = 0;

	ret = bq24296_read(client, reg, &retval, 1);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
		return ret;
	}

	if ((retval & mask) != value) {
		retval = ((retval & ~mask) | value) | value;
		ret = bq24296_write(client, reg, &retval, 1);
		if (ret < 0) {
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);
			return ret;
		}
	}

	return ret;
}

static int bq24296_init_registers(void)
{
	int ret = 0;

	/* reset the register */
	ret = bq24296_update_reg(bq24296_di->client,
				POWE_ON_CONFIGURATION_REGISTER,
				REGISTER_RESET_ENABLE << REGISTER_RESET_OFFSET,
				REGISTER_RESET_MASK << REGISTER_RESET_OFFSET);
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s(): Failed to reset the register \n",
				__func__);
		goto final;
	}

	mdelay(5);

	/* Disable the watchdog */
	ret = bq24296_update_reg(bq24296_di->client,
				TERMINATION_TIMER_CONTROL_REGISTER,
				WATCHDOG_DISABLE << WATCHDOG_OFFSET,
				WATCHDOG_MASK << WATCHDOG_OFFSET);
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s(): Failed to disable the watchdog \n",
				__func__);
		goto final;
	}

	/* Set Pre-Charge Current Limit as 128mA */
	ret = bq24296_update_reg(bq24296_di->client,
				PRE_CHARGE_TERMINATION_CURRENT_CONTROL_REGISTER,
				PRE_CHARGE_CURRENT_LIMIT_128MA << PRE_CHARGE_CURRENT_LIMIT_OFFSET,
				PRE_CHARGE_CURRENT_LIMIT_MASK << PRE_CHARGE_CURRENT_LIMIT_OFFSET);
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s(): Failed to set pre-charge limit 128mA \n",
				__func__);
		goto final;
	}

	/* Set Termination Current Limit as 128mA */
	ret = bq24296_update_reg(bq24296_di->client,
				PRE_CHARGE_TERMINATION_CURRENT_CONTROL_REGISTER,
				TERMINATION_CURRENT_LIMIT_128MA << TERMINATION_CURRENT_LIMIT_OFFSET,
				TERMINATION_CURRENT_LIMIT_MASK << TERMINATION_CURRENT_LIMIT_OFFSET);
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s(): Failed to set termination limit 128mA \n",
				__func__);
		goto final;
	}

final:
	return ret;
}

static int bq24296_get_limit_current(int value)
{
	u8 data;
	if (value < 120)
		data = 0;
	else if(value < 400)
		data = 1;
	else if(value < 700)
		data = 2;
	else if(value < 1000)
		data = 3;
	else if(value < 1200)
		data = 4;
	else if(value < 1800)
		data = 6;
	else
		data = 7;
	data &= 0xff;
	return data;
	
}

static int bq24296_get_chg_current(int value)
{
	u8 data;
		
	data = (value)/64;
	data &= 0xff;
	return data;	
}
static int bq24296_update_input_current_limit(u8 value)
{
	int ret = 0;
	ret = bq24296_update_reg(bq24296_di->client,
				INPUT_SOURCE_CONTROL_REGISTER,
				((value << IINLIM_OFFSET) | (EN_HIZ_DISABLE << EN_HIZ_OFFSET)),
				((IINLIM_MASK << IINLIM_OFFSET) | (EN_HIZ_MASK << EN_HIZ_OFFSET)));
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s(): Failed to set input current limit (0x%x) \n",
				__func__, value);
	}
	
	return ret;
}
 static int bq24296_set_charge_current(u8 value)
{
	int ret = 0;

	ret = bq24296_update_reg(bq24296_di->client,
				CHARGE_CURRENT_CONTROL_REGISTER,
				(value << CHARGE_CURRENT_OFFSET) ,(CHARGE_CURRENT_MASK <<CHARGE_CURRENT_OFFSET ));
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s(): Failed to set charge current limit (0x%x) \n",
				__func__, value);
	}
	return ret;
}
	
static int bq24296_update_en_hiz_disable(void)
{
	int ret = 0;

	ret = bq24296_update_reg(bq24296_di->client,
				INPUT_SOURCE_CONTROL_REGISTER,
				EN_HIZ_DISABLE << EN_HIZ_OFFSET,
				EN_HIZ_MASK << EN_HIZ_OFFSET);
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s(): Failed to set en_hiz_disable\n",
				__func__);
	}
	return ret;
}

int bq24296_set_input_current(int on)
{
	if(!bq24296_int)
		return 0;

	if(1 == on){
#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
		bq24296_update_input_current_limit(IINLIM_3000MA);
#else
		bq24296_update_input_current_limit(IINLIM_3000MA);
#endif
	}else{
		bq24296_update_input_current_limit(IINLIM_500MA);
	}
	DBG("bq24296_set_input_current %s\n", on ? "3000mA" : "500mA");

	return 0;
}
EXPORT_SYMBOL_GPL(bq24296_set_input_current);

static int bq24296_update_charge_mode(u8 value)
{
	int ret = 0;

	ret = bq24296_update_reg(bq24296_di->client,
				POWE_ON_CONFIGURATION_REGISTER,
				value << CHARGE_MODE_CONFIG_OFFSET,
				CHARGE_MODE_CONFIG_MASK << CHARGE_MODE_CONFIG_OFFSET);
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s(): Failed to set charge mode(0x%x) \n",
				__func__, value);
	}

	return ret;
}

static int bq24296_update_otg_mode_current(u8 value)
{
	int ret = 0;

	ret = bq24296_update_reg(bq24296_di->client,
				POWE_ON_CONFIGURATION_REGISTER,
				value << OTG_MODE_CURRENT_CONFIG_OFFSET,
				OTG_MODE_CURRENT_CONFIG_MASK << OTG_MODE_CURRENT_CONFIG_OFFSET);
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s(): Failed to set otg current mode(0x%x) \n",
				__func__, value);
	}
	return ret;
}

static int bq24296_charge_mode_config(int on)
{

	if(!bq24296_int)
		return 0;

	if(1 == on)
	{
		bq24296_update_en_hiz_disable();
		mdelay(5);
		bq24296_update_charge_mode(CHARGE_MODE_CONFIG_OTG_OUTPUT);
		mdelay(10);
		bq24296_update_otg_mode_current(OTG_MODE_CURRENT_CONFIG_1300MA);
	}else{
		bq24296_update_charge_mode(CHARGE_MODE_CONFIG_CHARGE_BATTERY);
	}

	DBG("bq24296_charge_mode_config is %s\n", on ? "OTG Mode" : "Charge Mode");

	return 0;
}
 int bq24296_charge_otg_en(int chg_en,int otg_en)
{
	int ret = 0;

	if ((chg_en ==0) && (otg_en ==0)){
		ret = bq24296_update_reg(bq24296_di->client,POWE_ON_CONFIGURATION_REGISTER,0x00 << 4,0x03 << 4);
	}
	else if ((chg_en ==0) && (otg_en ==1))
		bq24296_charge_mode_config(1);
	else 
		bq24296_charge_mode_config(0);
	return ret;
}

extern int dwc_vbus_status(void);
//extern int get_gadget_connect_flag(void);

static void usb_detect_work_func(struct work_struct *work)
{
	struct delayed_work *delayed_work = (struct delayed_work *)container_of(work, struct delayed_work, work);
	struct bq24296_device_info *pi = (struct bq24296_device_info *)container_of(delayed_work, struct bq24296_device_info, usb_detect_work);
	u8 retval = 0;
	int ret ;

	ret = bq24296_read(bq24296_di->client, 0x08, &retval, 1);
	if (ret < 0) {
		dev_err(&bq24296_di->client->dev, "%s: err %d\n", __func__, ret);
	}
	if ((retval & 0x30) ==0x30){
		bq24296_chag_down =1;
	}else
		bq24296_chag_down =0;

	DBG("%s: retval = %08x bq24296_chag_down = %d\n", __func__,retval,bq24296_chag_down);
	
	mutex_lock(&pi->var_lock);
	DBG("%s: dwc_vbus_status %d\n", __func__, dwc_vbus_status());
	switch(dwc_vbus_status())
		{
			case 2: // USB Wall charger
				bq24296_update_input_current_limit(bq24296_di->adp_input_current);
				bq24296_set_charge_current(bq24296_di->chg_current);
				bq24296_charge_mode_config(0);
				DBG("bq24296: detect usb wall charger\n");
			break;
			case 1: //normal USB
				#if 0
				if (0 == get_gadget_connect_flag()){  // non-standard AC charger
				bq24296_update_input_current_limit(IINLIM_2000MA);
				bq24296_set_charge_current(CHARGE_CURRENT_1024MA);
				bq24296_charge_mode_config(0);;
				}else{
				#endif
				// connect to pc	
				bq24296_update_input_current_limit(bq24296_di->usb_input_current);
				bq24296_set_charge_current(CHARGE_CURRENT_512MA);
				bq24296_charge_mode_config(0);
				DBG("bq24296: detect normal usb charger\n");
			//	}
			break;
			default:
				DBG("bq24296: detect no usb \n");			
			break;
		}
	mutex_unlock(&pi->var_lock);
	
	schedule_delayed_work(&pi->usb_detect_work, 1*HZ);
}


static void irq_work_func(struct work_struct *work)
{
//	struct bq24296_device_info *info= container_of(work, struct bq24296_device_info, irq_work);
}

static irqreturn_t chg_irq_func(int irq, void *dev_id)
{
//	struct bq24296_device_info *info = dev_id;
	DBG("%s\n", __func__);

//	queue_work(info->workqueue, &info->irq_work);

	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
static struct bq24296_board *bq24296_parse_dt(struct bq24296_device_info *di)
{
	struct bq24296_board *pdata;
	struct device_node *bq24296_np;
	
	DBG("%s,line=%d\n", __func__,__LINE__);
	bq24296_np = of_node_get(di->dev->of_node);
	if (!bq24296_np) {
		printk("could not find bq24296-node\n");
		return NULL;
	}
	pdata = devm_kzalloc(di->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;
	if (of_property_read_u32_array(bq24296_np,"bq24296,chg_current",pdata->chg_current, 3)) {
		printk("dcdc sleep voltages not specified\n");
		return NULL;
	}
	
	pdata->chg_irq_pin = of_get_named_gpio(bq24296_np,"gpios",0);
	if (!gpio_is_valid(pdata->chg_irq_pin)) {
		printk("invalid gpio: %d\n",  pdata->chg_irq_pin);
	}
	
	return pdata;
}

#else
static struct rk808_board *bq24296_parse_dt(struct bq24296_device_info *di)
{
	return NULL;
}
#endif

#ifdef CONFIG_OF
static struct of_device_id bq24296_battery_of_match[] = {
	{ .compatible = "ti,bq24296"},
	{ },
};
MODULE_DEVICE_TABLE(of, bq24296_battery_of_match);
#endif

static int bq24296_battery_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	struct bq24296_device_info *di;
	u8 retval = 0;
	struct bq24296_board *pdev;
	struct device_node *bq24296_node;
	int ret=0,irq=0;
	
	 DBG("%s,line=%d\n", __func__,__LINE__);
	 
	 bq24296_node = of_node_get(client->dev.of_node);
	if (!bq24296_node) {
		printk("could not find bq24296-node\n");
	}
	 
	di = devm_kzalloc( &client->dev,sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		retval = -ENOMEM;
		goto batt_failed_2;
	}
	i2c_set_clientdata(client, di);
	di->dev = &client->dev;
	di->client = client;	
	if (bq24296_node)
		pdev = bq24296_parse_dt(di);
	
	bq24296_pdata = pdev;
	
	 DBG("%s,line=%d chg_current =%d usb_input_current = %d adp_input_current =%d \n", __func__,__LINE__,
	 	pdev->chg_current[0],pdev->chg_current[1],pdev->chg_current[2]);
	 
	 /******************get set current******/
	if (pdev->chg_current[0] && pdev->chg_current[1] && pdev->chg_current[2]){
		di->chg_current = bq24296_get_chg_current(pdev->chg_current[0] );
		di->usb_input_current  = bq24296_get_limit_current(pdev->chg_current[1]);
		di->adp_input_current  = bq24296_get_limit_current(pdev->chg_current[2]);
	}
	else {
		di->chg_current = bq24296_get_chg_current(1000);
		di->usb_input_current  = bq24296_get_limit_current(500);
		di->adp_input_current  = bq24296_get_limit_current(2000);
	}
	/****************************************/
	bq24296_di = di;
	/* get the vendor id */
	ret = bq24296_read(di->client, VENDOR_STATS_REGISTER, &retval, 1);
	if (ret < 0) {
		dev_err(&di->client->dev, "%s(): Failed in reading register"
				"0x%02x\n", __func__, VENDOR_STATS_REGISTER);
		goto batt_failed_4;
	}
	di->workqueue = create_singlethread_workqueue("bq24296_irq");
	INIT_WORK(&di->irq_work, irq_work_func);
	mutex_init(&di->var_lock);
	INIT_DELAYED_WORK(&di->usb_detect_work, usb_detect_work_func);
	schedule_delayed_work(&di->usb_detect_work, 0);
	bq24296_init_registers();

	if (gpio_is_valid(pdev->chg_irq_pin)){
		irq = gpio_to_irq(pdev->chg_irq_pin);
		ret = request_threaded_irq(irq, NULL,chg_irq_func, IRQF_TRIGGER_FALLING| IRQF_ONESHOT, "bq24296_chg_irq", di);
		if (ret) {
			ret = -EINVAL;
			printk("failed to request bq24296_chg_irq\n");
			goto err_chgirq_failed;
		}
	}

	bq24296_int =1;

	DBG("bq24296_battery_probe ok");
	return 0;

batt_failed_4:
	kfree(di);
batt_failed_2:
	
err_chgirq_failed:
	free_irq(gpio_to_irq(pdev->chg_irq_pin), NULL);
	return retval;
}

static void bq24296_battery_shutdown(struct i2c_client *client)
{
	free_irq(gpio_to_irq(bq24296_pdata->chg_irq_pin), NULL);
	
}
static int bq24296_battery_remove(struct i2c_client *client)
{
	struct bq24296_device_info *di = i2c_get_clientdata(client);
	kfree(di);
	return 0;
}

static const struct i2c_device_id bq24296_id[] = {
	{ "bq24296", 0 },
};

static struct i2c_driver bq24296_battery_driver = {
	.driver = {
		.name = "bq24296",
		.owner = THIS_MODULE,
		.of_match_table =of_match_ptr(bq24296_battery_of_match),
	},
	.probe = bq24296_battery_probe,
	.remove = bq24296_battery_remove,
	.shutdown = bq24296_battery_shutdown,
	.id_table = bq24296_id,
};

static int __init bq24296_battery_init(void)
{
	int ret;
	
	ret = i2c_add_driver(&bq24296_battery_driver);
	if (ret)
		printk(KERN_ERR "Unable to register BQ24296 driver\n");
	
	return ret;
}
module_init(bq24296_battery_init);

static void __exit bq24296_battery_exit(void)
{
	i2c_del_driver(&bq24296_battery_driver);
}
module_exit(bq24296_battery_exit);

MODULE_AUTHOR("Rockchip");
MODULE_DESCRIPTION("BQ24296 battery monitor driver");
MODULE_LICENSE("GPL");
