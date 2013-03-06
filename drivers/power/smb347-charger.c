/*
 * smb347 battery driver
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
#include <mach/gpio.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <mach/board.h>
#include <mach/iomux.h>
#include <linux/power/smb347-charger.h>

#include <linux/interrupt.h>
#include "../usb/dwc_otg/dwc_otg_driver.h"

#if 1
#define xhc_printk(format, ...)       printk(format, ## __VA_ARGS__)
#else 
#define xhc_printk(format, ...)
#endif

#define SMB347_STATUS_D                 0x3d
#define SMB347_SPEED                    (300 * 1000) 
#define MAX_REG_INDEX                   0x3f  

struct workqueue_struct *wq;
struct smb347_device{
        struct i2c_client *client;
        struct delayed_work work;
        struct smb347_info *info;
        struct work_struct full_power_work_struct;
        int usb_host_in;
};


/* Input current limit in mA */
static const unsigned int icl_tbl[] = {
        300,
        500,
        700,
        900,
        1200,
        1500,
        1800,
        2000,
        2200,
        2500,
};

extern dwc_otg_device_t* g_otgdev;
struct smb347_device * g_smb347_dev;
static void smb347_init(struct i2c_client *client);

static int smb347_read(struct i2c_client *client, const char reg, char *buf, int len)
{
	int ret;
	ret = i2c_master_reg8_recv(client, reg, buf, len, SMB347_SPEED);
	return ret; 
}

static int smb347_write(struct i2c_client *client,const char reg, char *buf, int len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, len, SMB347_SPEED);
	return ret;
}

static int dump_smb347_reg(struct smb347_device *dev)
{
        int ret = 0;
        char buf = 0;
        int reg = 0;
        if(!dev)
        {
                xhc_printk("dev is null");
                return -1;
        }
	for(reg = 0; reg <= MAX_REG_INDEX; reg++)
	{
        	ret = i2c_master_reg8_recv(dev->client, reg, &buf, 1, SMB347_SPEED);
		
        	if(ret < 0)
        	{
                	printk("read smb137 reg error:%d\n",ret);
        	}
        	else
        	{
                	printk("reg 0x%x:0x%x\n",reg,buf);
        	}
	}

	return 0;

}

static ssize_t smb_debug_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t _count)
{
        int temp;
	u8 reg;
	u8 val;
	struct smb347_device *smb347_dev = dev_get_drvdata(dev);
        if (sscanf(buf, "%x", &temp) != 1)
                return -EINVAL;
        val = temp & 0x00ff;
	reg = temp >> 8;
	smb347_write(smb347_dev->client, reg, &val,1);
	
        return _count;
}

static ssize_t smb_debug_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct smb347_device *smb347_dev = dev_get_drvdata(dev);
	dump_smb347_reg(smb347_dev);
	return 0;
}

static struct device_attribute smb_debug = 
	__ATTR(smb_debug, S_IRUGO | S_IWUSR, smb_debug_show, smb_debug_store);

int smb347_is_chg_ok(void)
{
        u8 reg = 0;
	int ret = 0;

        smb347_read(g_smb347_dev->client, 0x37, &reg, 1);
        ret = (reg & 0x03);

	return ret;
}


EXPORT_SYMBOL(smb347_is_chg_ok);

int smb347_is_charging(void)
{
	int status = 0;//POWER_SUPPLY_STATUS_UNKNOWN;
	u8 data = 0;

	smb347_read(g_smb347_dev->client, SMB347_STATUS_D, &data, 1);
	if (data & 0x06)
		status = 1;
        
	return status;
}

EXPORT_SYMBOL(smb347_is_charging);

void smb347_set_something(void)
{
	u8 reg;

	smb347_init(g_smb347_dev->client);
	return;
}

EXPORT_SYMBOL(smb347_set_something);

void smb347_set_charging(void)
{
	u8 val;

        val = 0x26;
	smb347_write(g_smb347_dev->client, 0x03, &val, 1);

        smb347_read(g_smb347_dev->client, 0x04, &val, 1);
        val &= 0x7f;
	smb347_write(g_smb347_dev->client, 0x04, &val, 1);

	return;
}

EXPORT_SYMBOL(smb347_set_charging);

void smb347_set_discharging(void)
{
	u8 val;

        val = 0x20;
	smb347_write(g_smb347_dev->client, 0x03, &val, 1);

        smb347_read(g_smb347_dev->client, 0x04, &val, 1);
        val |= 0x80;
	smb347_write(g_smb347_dev->client, 0x04, &val, 1);

	return;
}

EXPORT_SYMBOL(smb347_set_discharging);

/* Convert current to register value using lookup table */
static int current_to_hw(const unsigned int *tbl, size_t size, unsigned int val)
{
        size_t i;
        for (i = 0; i < size; i++) {
                if (val < tbl[i]) {
                        break;
                }
        }

        return i > 0 ? i - 1 : -EINVAL;
}

static int smb347_set_current_limits(struct smb347_device *smb_dev)
{
        char ret;
        if (smb_dev->info->max_current) {
                xhc_printk("xhc_test_smb_dev->info->max_current = %d\n", smb_dev->info->max_current);
                ret = current_to_hw (icl_tbl, ARRAY_SIZE(icl_tbl),
                                smb_dev->info->max_current);
                if (ret < 0) {
                        return ret;
                }
                ret = (ret << 4) + ret;
        	ret = 0x77; //Hardcode 2000mA 2012-11-06
                xhc_printk("ret = %x\n", ret);
                ret = smb347_write(smb_dev->client, 0x01, &ret, 1);
                xhc_printk("ret = %x\n", ret);
                if (ret < 0) {
                        return ret;
                }
        } 
        return 0;
}

static void suspend_smb347(struct smb347_device *smb347_dev)
{
        u8 reg;
        reg = 0x80;
	smb347_write(smb347_dev->client,0x30,&reg,1);
        smb347_read(smb347_dev->client, 0x02, &reg, 1);
        reg = (reg&0x7f);
	smb347_write(smb347_dev->client,0x02,&reg,1);
        xhc_printk("%s\n", __func__);
}

static void active_smb347(struct smb347_device *smb347_dev)
{
        u8 reg;
        reg = 0x80;
	smb347_write(smb347_dev->client,0x30,&reg,1);
        smb347_read(smb347_dev->client, 0x02, &reg, 1);
        reg = (reg | 0x80);
	smb347_write(smb347_dev->client,0x02,&reg,1);
        xhc_printk("%s\n", __func__);
}

static int smb347_set_otg_control(struct smb347_device *smb_dev)
{
        char ret;
        char reg;
        if (smb_dev->info->otg_power_form_smb == 1) {
                ret = smb347_read(smb_dev->client, 0x09, &reg, 1);
                if (ret < 0) {
                        xhc_printk("error,ret = %x\n", ret);
                        return ret;
                }
        	reg &= 0xef;  
                reg |= 0x40; 
                reg |= 0x20;  
	        ret = smb347_write(smb_dev->client,0x09,&reg,1);	
        	if (ret < 0) {
                        xhc_printk("error,ret = %x\n", ret);
                        return ret;
                }
                reg = 0x76;
        	smb347_write(smb_dev->client,0x0a,&reg,1);
                if (ret < 0) {
                        xhc_printk("error,ret = %x\n", ret);
                        return ret;
                }
        }
        return 0;
}


static void smb347_init(struct i2c_client *client)  
{
	u8 reg;
        reg = 0x80;
	smb347_write(client, 0x30, &reg, 1);

        reg = 0xfd;
	smb347_write(client, 0x00, &reg, 1);

        reg = 0x77;
	smb347_write(client, 0x01, &reg, 1);

        reg = 0x26;
	smb347_write(client, 0x03, &reg, 1);

        smb347_read(client, 0x05, &reg, 1);
        reg |= 0x80;
	smb347_write(client, 0x05, &reg, 1);

        /* close interrupt */
        smb347_read(client, 0x38, &reg, 1);
        smb347_read(client, 0x3a, &reg, 1);
        reg = 0x0;
        smb347_write(client, 0x0c, &reg, 1);
        smb347_write(client, 0x0d, &reg, 1);
        
	/* set dc charge when bosh inser dc and usb */
	smb347_read(client, 0x02, &reg, 1);
	reg = reg & 0xfb;
	smb347_write(client, 0x02, &reg, 1);


        smb347_set_otg_control(g_smb347_dev);
        smb347_set_current_limits(g_smb347_dev);
        
	dump_smb347_reg(g_smb347_dev);
}

static void smb347_set_current_work(struct work_struct *work)
{
        struct smb347_device *smb347_dev = container_of(to_delayed_work(work), struct smb347_device, work);
        u8 reg;
        if (g_otgdev->core_if->op_state == A_HOST && smb347_dev->usb_host_in == 0) {

                xhc_printk("otg_dev->core_if->op_state = %d\n", g_otgdev->core_if->op_state);
                if (g_smb347_dev->info->otg_power_form_smb == 1) {

        	        reg = 0x7e;
                	smb347_write(smb347_dev->client,0x0a,&reg,1);
                } else {
                        suspend_smb347(smb347_dev);        
                }
                smb347_dev->usb_host_in = 1;
        } else if (g_otgdev->core_if->op_state != A_HOST && smb347_dev->usb_host_in == 1) {

                xhc_printk("otg_dev->core_if->op_state = %d\n", g_otgdev->core_if->op_state);
                if (g_smb347_dev->info->otg_power_form_smb == 1) {
	                reg = 0x76;
        	        smb347_write(smb347_dev->client,0x0a,&reg,1);
                } else {
                        active_smb347(smb347_dev);        
                }
                smb347_dev->usb_host_in = 0;
        }
        schedule_delayed_work(&smb347_dev->work, 100);       
}

static int smb347_battery_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int ret;
	struct smb347_device *smb347_dev;
	struct smb347_info *info = client->dev.platform_data;;
	
        xhc_printk("__xhc__%s, line = %d\n", __func__, __LINE__);
        smb347_dev = kzalloc(sizeof(struct smb347_device), GFP_KERNEL);
        smb347_dev->usb_host_in = 0;
        if (!smb347_dev) {
                dev_err(&client->dev, "failed to allocate device info data\n");
                ret = -ENOMEM;
                return ret;
        }

        xhc_printk("__xhc__%s, line = %d\n", __func__, __LINE__);
        i2c_set_clientdata(client, smb347_dev);
	dev_set_drvdata(&client->dev,smb347_dev);
        smb347_dev->client = client;
	smb347_dev->info = info;
	g_smb347_dev = smb347_dev;
        wq = create_singlethread_workqueue("smb347_det");

	if(info->chg_susp_pin) {
		rk30_mux_api_set(GPIO4D1_SMCDATA9_TRACEDATA9_NAME, 0);
		ret = gpio_request(info->chg_susp_pin, "chg susp pin");
		if (ret != 0) {
			gpio_free(info->chg_susp_pin);
			xhc_printk("smb347 gpio_request chg_susp_pin error\n");
			return -EIO;
		}
		gpio_direction_output(info->chg_susp_pin, 0);
		gpio_set_value(info->chg_susp_pin, GPIO_HIGH);
	}
        //msleep(200);
	if(info->chg_ctl_pin) {
		ret = gpio_request(info->chg_ctl_pin, "chg ctl pin");
		if (ret != 0) {
			gpio_free(info->chg_ctl_pin);
			xhc_printk("smb347 gpio_request chg_ctl_pin error\n");
			return -EIO;
		}
                xhc_printk("__xhc__%s, line = %d\n", __func__, __LINE__);
		gpio_direction_output(info->chg_ctl_pin, 0);
		// gpio_set_value(info->chg_ctl_pin, GPIO_HIGH);
	}

	if(info->chg_en_pin)
	{
		rk30_mux_api_set(GPIO4D5_SMCDATA13_TRACEDATA13_NAME, 0);
		ret = gpio_request(info->chg_en_pin, "chg en pin");
		if (ret != 0) {
			gpio_free(info->chg_en_pin);
			xhc_printk("smb347 gpio_request chg_en_pin error\n");
			return -EIO;
		}
		gpio_direction_output(info->chg_en_pin, 0);
		gpio_set_value(info->chg_en_pin, GPIO_LOW);
	}
	mdelay(100);
	smb347_init(client);

	INIT_DELAYED_WORK(&smb347_dev->work,smb347_set_current_work);
        schedule_delayed_work(&smb347_dev->work, msecs_to_jiffies(3*1000));	

        ret = device_create_file(&client->dev,&smb_debug);
	if(ret) {
		dev_err(&client->dev, "failed to create sysfs file\n");
		return ret;
	}
	
	return 0;
}

static int smb347_battery_remove(struct i2c_client *client)
{
        return 0;
}

static int smb347_battery_suspend(struct i2c_client *client, pm_message_t mesg)
{
        xhc_printk("__xhc__%s,", __func__);
        return 0; 
}

static int smb347_battery_resume(struct i2c_client *client)
{
        xhc_printk("__xhc__%s,", __func__);
        return 0;
}
static  void smb347_battery_shutdown(struct i2c_client *client)
{
	u8 reg = 0x0e;
	smb347_write(client,0x09,&reg,1);
	xhc_printk("%s,----xhc----\n", __func__);
}
static const struct i2c_device_id smb347_id[] = {
	{ "smb347", 0 },
	{}
};

static struct i2c_driver smb347_battery_driver = {
	.probe   = smb347_battery_probe,
        .remove  = smb347_battery_remove,
        .suspend = smb347_battery_suspend,
        .resume  = smb347_battery_resume,
        .shutdown = smb347_battery_shutdown,

	.id_table = smb347_id,
	.driver = {
		.name = "smb347",
	},
};

static int __init smb347_battery_init(void)
{
	int ret;
	
	ret = i2c_add_driver(&smb347_battery_driver);
	if (ret)
		xhc_printk(KERN_ERR "Unable to register smb347 driver\n");
	
	return ret;
}

static void __exit smb347_battery_exit(void)
{
        if (g_smb347_dev->info->otg_power_form_smb != 1) {
                active_smb347(g_smb347_dev);
        }
	i2c_del_driver(&smb347_battery_driver);   
}

//subsys_initcall_sync(smb347_battery_init);
subsys_initcall(smb347_battery_init);
module_exit(smb347_battery_exit);

/*
   delay 500ms to fix the problam 
   that sometime limit 500ma when startup when insert the hc charger 
 */
static int __init delay_for_smb347(void)
{
	xhc_printk("function: %s\n", __func__);
	mdelay(500);
        return 0;
}
core_initcall(delay_for_smb347);

MODULE_AUTHOR("xhc@rock-chips.com");
MODULE_DESCRIPTION("smb347 battery monitor driver");
MODULE_LICENSE("GPL");
