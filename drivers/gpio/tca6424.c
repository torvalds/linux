/*
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*******************************************************************/
/*	  COPYRIGHT (C)  ROCK-CHIPS FUZHOU . ALL RIGHTS RESERVED.*/
/*******************************************************************
FILE			:	tca6424.c
MODIFY		:	sxj
DATE		:	2010-8-11
NOTES		:
********************************************************************/
#include <asm/mach/time.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <asm/mach-types.h>
#include <linux/irq.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/rk2818_iomap.h>
#include <mach/iomux.h>
#include <linux/device.h>
#include <mach/gpio.h>
#include <asm/gpio.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <mach/board.h>
#include <linux/delay.h>
#include <linux/i2c/tca6424.h>
#include <linux/ktime.h>
#include "../drivers/gpio/expand_gpio_soft_interrupt.h"

#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif
#define DBGERR(x...)	printk(KERN_INFO x)


struct tca6424_chip {
	
	unsigned int gpio_start;
	unsigned int gpio_pin_num;

	#ifdef TCA6424_OUTREGLOCK
		struct mutex outreglock;
	#endif
	#ifdef TCA6424_INPUTREGLOCK
		struct mutex inputreglock;
	#endif
	#ifdef TCA6424_CONFIGREGLOCK
		struct mutex configreglock;
	#endif

	struct i2c_client *client;
	struct expand_gpio_soft_int *expand;
	struct expand_gpio_global_variable gtca6424_struct;
	struct gpio_chip gpio_chip;
	char **names;
};

static const struct i2c_device_id tca6424_id[] = 
{
    {"extend_gpio_tca6424",8,}, 
    { }
};
MODULE_DEVICE_TABLE(i2c, tca6424_id);

static short int portnum[TCA6424_PortNum]={ TCA6424_Port0PinNum,
                                            TCA6424_Port1PinNum,TCA6424_Port2PinNum};

extern inline struct gpio_chip *gpio_to_chip(unsigned gpio);

int tca6424_irq_read_inputreg(void *data,char *buf)
{

	struct tca6424_chip *tca6424data=(struct tca6424_chip *)data;
	int ret = -1;
	ret = i2c_master_reg8_recv(tca6424data->client, TCA6424_Auto_InputLevel_Reg, buf, 3, TCA6424_I2C_RATE);
	return (ret>0)?0:ret;
}

static int tca6424_write_reg(struct i2c_client *client, uint8_t reg, uint8_t val)
{
	int ret=-1;
	struct i2c_adapter *adap;
	struct i2c_msg msg;
	char tx_buf[2];
	if(!client)
		return ret;    
	adap = client->adapter;		
	tx_buf[0] = reg;
	tx_buf[1]= val;

	msg.addr = client->addr;
	msg.buf = tx_buf;
	msg.len = 1 +1;
	msg.flags = client->flags;   
	msg.scl_rate = TCA6424_I2C_RATE;
	ret = i2c_transfer(adap, &msg, 1);
	return ret;  
}

static int tca6424_read_reg(struct i2c_client *client, uint8_t reg, uint8_t *val)
{
  	int ret=-1;
    struct i2c_adapter *adap;
    struct i2c_msg msgs[2];
	
    if(!client)
		return ret;    
    adap = client->adapter;		
    //发送寄存器地址
    msgs[0].addr = client->addr;
    msgs[0].buf = &reg;
    msgs[0].flags = client->flags;
    msgs[0].len = 1;
    msgs[0].scl_rate = TCA6424_I2C_RATE;
    //接收数据
    msgs[1].buf = val;
    msgs[1].addr = client->addr;
    msgs[1].flags = client->flags | I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].scl_rate = TCA6424_I2C_RATE;

    ret = i2c_transfer(adap, msgs, 2);
    return ret;   

}

static int tca6424_write_three_reg(struct i2c_client *client, const char reg, const char *buf, int count, int rate)
{
	int ret = -1;
	ret = i2c_master_reg8_send(client, reg, buf, count, rate);
	return (ret>0)?0:ret;
}

static int tca6424_read_three_reg(struct i2c_client *client, const char reg, char *buf, int count, int rate)
{
	int ret = -1;
	ret = i2c_master_reg8_recv(client, reg, buf, count, rate);
	return (ret>0)?0:ret;
}

static int tca6424_gpio_direction_input(struct gpio_chip *gc, unsigned pin_num)
{
	struct tca6424_chip *chip;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;
	uint8_t reg_val; 
	uint8_t Regaddr;
	int ret = -1;
	
	chip = container_of(gc, struct tca6424_chip, gpio_chip);
	gpioPortNum = pin_num/8;
	gpioPortPinNum= pin_num%8;
	if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
		return ret;
	Regaddr = TCA6424_Config_Reg+gpioPortNum;	
	
	#ifdef TCA6424_CONFIGREGLOCK
	if (!mutex_trylock(&chip->configreglock))
	{
		DBGERR("**%s[%d]Did not get the configreglock**\n",__FUNCTION__,__LINE__);
		return -1;
	}
	#endif

	if(((chip->gtca6424_struct.reg_direction[gpioPortNum]>>gpioPortPinNum)& 0x01)==EXTGPIO_OUTPUT)
	{
		reg_val = tca6424setbit(chip->gtca6424_struct.reg_direction[gpioPortNum], gpioPortPinNum);
		ret = tca6424_write_reg(chip->client, Regaddr, reg_val);
		if(ret<0)
			goto err;
		chip->gtca6424_struct.reg_direction[gpioPortNum] = reg_val;
		//DBG("**%s[%d],set config address[0x%2x]=%2x,ret=%d**\n",__FUNCTION__,__LINE__,Regaddr,reg_val,ret);	
	}
err:
	#ifdef TCA6424_CONFIGREGLOCK
		mutex_unlock(&chip->configreglock);
	#endif
	return (ret<0)?-1:0;
}

static int tca6424_gpio_direction_output(struct gpio_chip *gc, unsigned pin_num, int val)
{
	struct tca6424_chip *chip;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t reg_val = 0;
	uint8_t Regaddr = 0;
	int ret = -1;
	
	chip = container_of(gc, struct tca6424_chip, gpio_chip);
	gpioPortNum = pin_num/8;
	gpioPortPinNum = pin_num%8;
	if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
		return ret;
	Regaddr = TCA6424_Config_Reg+gpioPortNum;	

	#ifdef TCA6424_CONFIGREGLOCK
	if (!mutex_trylock(&chip->configreglock))
	{
		DBGERR("**%s[%d]Did not get the configreglock**\n",__FUNCTION__,__LINE__);
		return -1;
	}
	#endif

	if(((chip->gtca6424_struct.reg_direction[gpioPortNum]>>gpioPortPinNum)& 0x01)==EXTGPIO_INPUT)
	{
		reg_val = tca6424clearbit(chip->gtca6424_struct.reg_direction[gpioPortNum], gpioPortPinNum);
		//DBG("**%s[%d],set config address[0x%2x]=%2x,**\n",__FUNCTION__,__LINE__,Regaddr,reg_val);	
		ret = tca6424_write_reg(chip->client, Regaddr, reg_val);
		if(ret<0)
		{
			#ifdef TCA6424_CONFIGREGLOCK
			mutex_unlock(&chip->configreglock);
			#endif
			DBGERR("**%s[%d] set direction reg is error,reg_val=%x,ret=%d**\n",__FUNCTION__,__LINE__,reg_val,ret);
    		return ret;
		}
		chip->gtca6424_struct.reg_direction[gpioPortNum] = reg_val;
	}
	
	#ifdef TCA6424_CONFIGREGLOCK
	mutex_unlock(&chip->configreglock);
	#endif
    	
	ret = -1;
    #ifdef TCA6424_OUTREGLOCK
	if (!mutex_trylock(&chip->outreglock))
	{
		DBGERR("**%s[%d] Did not get the outreglock**\n",__FUNCTION__,__LINE__);
		return ret;
	}
	#endif

	if(((chip->gtca6424_struct.reg_output[gpioPortNum]>>gpioPortPinNum)& 0x01) != val)
	{
		if (val)
			reg_val = tca6424setbit(chip->gtca6424_struct.reg_output[gpioPortNum], gpioPortPinNum);
		else
			reg_val = tca6424clearbit(chip->gtca6424_struct.reg_output[gpioPortNum], gpioPortPinNum);

		Regaddr = TCA6424_OutputLevel_Reg+gpioPortNum;	
		ret = tca6424_write_reg(chip->client, Regaddr, reg_val);
		if (ret<0)
		{
			#ifdef TCA6424_OUTREGLOCK
				mutex_unlock(&chip->outreglock);
			#endif
			DBGERR("**%s[%d] set out reg is error,reg_val=%x,ret=%d**\n",__FUNCTION__,__LINE__,reg_val,ret);
			return ret;
		}
		chip->gtca6424_struct.reg_output[gpioPortNum] = reg_val;
	}

	#ifdef TCA6424_OUTREGLOCK
		mutex_unlock(&chip->outreglock);
	#endif
	//DBG("**%s[%d],set output address[0x%2x]=%2x,ret=%d**\n",__FUNCTION__,__LINE__,Regaddr,reg_val,ret);	
    return (ret<0)?-1:0;
}

static int tca6424_gpio_get_value(struct gpio_chip *gc, unsigned pin_num)
{
	struct tca6424_chip *chip;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;
	uint8_t Regaddr;
	int ret;

	chip = container_of(gc, struct tca6424_chip, gpio_chip);

	#ifdef CONFIG_EXPAND_GPIO_SOFT_INTERRUPT
	ret = wait_untill_input_reg_flash( );
	if(ret<0)
	{
		return -1;
		DBGERR("**********tca6424 get value error***************\n");
	}
	#endif
	
	gpioPortNum = pin_num/8;
	gpioPortPinNum= pin_num%8;
	Regaddr = TCA6424_OutputLevel_Reg+gpioPortNum;	
	
	if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
		return -1;

	#ifndef CONFIG_EXPAND_GPIO_SOFT_INTERRUPT
	uint8_t reg_val;
	ret = tca6424_read_reg(chip->client, Regaddr, &reg_val);
	if (ret < 0) 
		return -1;
	chip->gtca6424_struct.reg_input[gpioPortNum] = reg_val;
	#endif

	//DBG("**%s[%d] read input address[0x%2x]=%2x**\n",__FUNCTION__,__LINE__,Regaddr,chip->reg_input[gpioPortNum]);
	return ((chip->gtca6424_struct.reg_input[gpioPortNum] >> gpioPortPinNum) & 0x01);
}

static void tca6424_gpio_set_value(struct gpio_chip *gc, unsigned pin_num, int val)
{
	struct tca6424_chip *chip;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;
	uint8_t reg_val;
	uint8_t Regaddr;
	int ret=-1;
	
	chip = container_of(gc, struct tca6424_chip, gpio_chip);
	gpioPortNum = pin_num/8;
	gpioPortPinNum= pin_num%8;
	if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
		return;
	Regaddr = TCA6424_OutputLevel_Reg+gpioPortNum;	
	if(tca6424getbit(chip->gtca6424_struct.reg_direction[gpioPortNum],gpioPortPinNum)) // input state
	{
		return;
	}
		
	#ifdef TCA6424_OUTREGLOCK
	if (!mutex_trylock(&chip->outreglock))
	{
		DBGERR("**%s[%d] Did not get the outreglock**\n",__FUNCTION__,__LINE__);
		return;
	}
	#endif
	if(((chip->gtca6424_struct.reg_output[gpioPortNum]>>gpioPortPinNum)& 0x01) != val)
	{
		if(val)
			reg_val = tca6424setbit(chip->gtca6424_struct.reg_output[gpioPortNum], gpioPortPinNum);
		else
			reg_val = tca6424clearbit(chip->gtca6424_struct.reg_output[gpioPortNum], gpioPortPinNum);

		ret = tca6424_write_reg(chip->client, Regaddr, reg_val);
		if (ret<0)
			goto err;
		chip->gtca6424_struct.reg_output[gpioPortNum] = reg_val;
		//DBG("**%s[%d],set output address[0x%2x]=%2x,ret=%d**\n",__FUNCTION__,__LINE__,Regaddr,reg_val,ret);
	}
err: 
	#ifdef TCA6424_OUTREGLOCK
		mutex_unlock(&chip->outreglock);
	#endif
	return;
}

int tca6424_checkrange(int start,int num,int val)
{   
	if((val<(start+num))&&(val>=start))
		return 0;
	else 
		return -1;
}

static int tca6424_gpio_to_irq(struct gpio_chip *chip,unsigned offset)
{
    struct tca6424_chip *pchip = container_of(chip, struct tca6424_chip, gpio_chip);
	if(((pchip->gpio_start+offset)>=chip->base)&&((pchip->gpio_start+offset)<(chip->base+chip->ngpio)))
	{
  		//DBG("**%s,offset=%d,gpio_irq_start=%d,base=%d,ngpio=%d,gpio_irq_start=%d**\n",
		//	__FUNCTION__,offset,pchip->expand->gpio_irq_start,chip->base,chip->ngpio,pchip->expand->gpio_irq_start);
		return (offset+pchip->expand->gpio_irq_start);
	}
	else
	{
		return -1;
	}
}

static void tca6424_setup_gpio(struct tca6424_chip *chip, int gpios)
{
    struct gpio_chip *gc;
    gc = &chip->gpio_chip;
    gc->direction_input  = tca6424_gpio_direction_input;
    gc->direction_output = tca6424_gpio_direction_output;
    gc->get = tca6424_gpio_get_value;
    gc->set = tca6424_gpio_set_value;
    gc->to_irq = tca6424_gpio_to_irq;
    gc->can_sleep = 1;
    gc->base = chip->gpio_start;
    gc->ngpio = chip->gpio_pin_num;
    gc->label = chip->client->name;
    gc->dev = &chip->client->dev;
    gc->owner = THIS_MODULE;
    gc->names = chip->names;
}

int tca6424_init_pintype(struct tca6424_chip *chip,struct i2c_client *client)
{
	struct tca6424_platform_data *platform_data=(struct tca6424_platform_data *)client->dev.platform_data;
	struct rk2818_gpio_expander_info *tca6424_gpio_settinginfo;
	uint8_t reg_output[TCA6424_PortNum]={0,0,0};
	uint8_t reg_direction[TCA6424_PortNum]={0,0,0};
	uint8_t reg_invert[TCA6424_PortNum]={0,0,0};
	uint8_t tca6424_pin_num;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum,tca6424_settingpin_num=0;
	int i = 0;
	if(platform_data)
	{
		tca6424_gpio_settinginfo=platform_data->settinginfo;
		if(tca6424_gpio_settinginfo)
		{
			tca6424_settingpin_num=platform_data->settinginfolen;
			for(i=0;i<tca6424_settingpin_num;i++)
			{
				if(!tca6424_checkrange(chip->gpio_start,chip->gpio_pin_num,tca6424_gpio_settinginfo[i].gpio_num))
				{
					tca6424_pin_num=tca6424_gpio_settinginfo[i].gpio_num-chip->gpio_start;
					gpioPortNum = tca6424_pin_num/ TCA6424_PortPinNum;
					gpioPortPinNum= tca6424_pin_num% TCA6424_PortPinNum;
					//DBG("gpioPortNum=%d,gpioPortNum=%d,tca6424_pin_num=%d,reg_direction=%x,reg_output=%x,reg_input=%x\n",gpioPortNum,gpioPortPinNum,tca6424_pin_num,reg_direction[i],reg_output[i]);
					if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
						continue;
					if(tca6424_gpio_settinginfo[i].pin_type==GPIO_IN)
					{
						reg_direction[gpioPortNum]=tca6424setbit(reg_direction[gpioPortNum],gpioPortPinNum);
					}
					else
					{
						if(tca6424_gpio_settinginfo[i].pin_value==GPIO_HIGH)
						{
							reg_output[gpioPortNum]=tca6424setbit(reg_output[gpioPortNum],gpioPortPinNum);
						}
					}
					
				}
			}
		}
	}
	#ifdef TCA6424_OUTREGLOCK
	mutex_init(&chip->outreglock);
    #endif 
	#ifdef TCA6424_INPUTREGLOCK
	mutex_init(&chip->inputreglock);
	#endif
	#ifdef TCA6424_OUTREGLOCK
	mutex_init(&chip->configreglock);
	#endif

	if(tca6424_write_three_reg(client, TCA6424_Auto_Config_Reg , &reg_direction[0], 3, TCA6424_I2C_RATE)<0)
	{
		DBGERR("*%s %d* write reg err\n",__FUNCTION__,__LINE__);
		return -1;
	}
	if (tca6424_write_three_reg(client, TCA6424_Auto_OutputLevel_Reg, &reg_output[0], 3, TCA6424_I2C_RATE)<0)
	{
		DBGERR("*%s %d* write reg err\n",__FUNCTION__,__LINE__);
		return -1;
	}
	if (tca6424_write_three_reg(client, TCA6424_Auto_Invert_Reg, &reg_invert[0], 3, TCA6424_I2C_RATE)<0)		//make sure this reg be 0
	{
		DBGERR("*%s %d* write reg err\n",__FUNCTION__,__LINE__);
		return -1;
	}
	if(tca6424_read_three_reg(client, TCA6424_Auto_InputLevel_Reg, &chip->gtca6424_struct.reg_input[0], 3, TCA6424_I2C_RATE)<0)
	{
		DBGERR("*%s %d  read reg err*\n",__FUNCTION__,__LINE__);
		return -1;
	}	  
	for(i=0; i<TCA6424_PortNum; i++)
	{
		chip->gtca6424_struct.reg_direction[i]=reg_direction[i];
		chip->gtca6424_struct.reg_output[i]=reg_output[i];
		DBG("reg_direction=%x,reg_output=%x,reg_input=%x\n",chip->gtca6424_struct.reg_direction[i],chip->gtca6424_struct.reg_output[i],chip->gtca6424_struct.reg_input[i]);
	}
	return 0;
}

static int __devinit tca6424_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	struct tca6424_chip *chip;
	struct tca6424_platform_data  *pdata;
	int ret;
	DBG(KERN_ALERT"*******gpio %s in %d line,dev adr is %x**\n",__FUNCTION__,__LINE__,client->addr);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	chip = kzalloc(sizeof(struct tca6424_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	pdata = client->dev.platform_data;
	if (pdata == NULL) {
		DBGERR(" %s no platform data\n",__FUNCTION__);
		ret = -EINVAL;
		goto out_failed;
	}
	//used by old tca6424,it will remove later
	client->adapter->dev.platform_data = pdata;
	
	chip->gpio_start = pdata->gpio_base;
	chip->gpio_pin_num=pdata->gpio_pin_num;
	chip->client = client;
	chip->names = pdata->names;

	#ifdef CONFIG_EXPAND_GPIO_SOFT_INTERRUPT
	chip->expand = &expand_irq_data;
	chip->expand->gpio_irq_start =pdata->gpio_irq_start;
	chip->expand->irq_pin_num = pdata->irq_pin_num;
	chip->expand->irq_gpiopin=pdata->tca6424_irq_pin;
	chip->expand->irq_chain = gpio_to_irq(pdata->tca6424_irq_pin);
	chip->expand->expand_port_group   = pdata->expand_port_group;
	chip->expand->expand_port_pinnum = pdata->expand_port_pinnum;
	chip->expand->rk_irq_mode =  pdata->rk_irq_mode;
	chip->expand->rk_irq_gpio_pull_up_down = pdata->rk_irq_gpio_pull_up_down;
	#endif
	
	/* initialize cached registers from their original values.
	* we can't share this chip with another i2c master.
	*/
	tca6424_setup_gpio(chip, id->driver_data);
	ret = gpiochip_add(&chip->gpio_chip);
	if (ret)
		goto out_failed;
	if(tca6424_init_pintype(chip,client))
		goto out_failed;
	if (pdata->setup) {
		ret = pdata->setup(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0)
			DBGERR(" %s setup failed, %d\n",__FUNCTION__,ret);
	}
	i2c_set_clientdata(client, chip);

	#ifdef CONFIG_EXPAND_GPIO_SOFT_INTERRUPT
	expand_irq_init(chip,&chip->gtca6424_struct,tca6424_irq_read_inputreg);
	#endif
	return 0;
out_failed:
	kfree(chip);
    return 0;
}

static int tca6424_remove(struct i2c_client *client)
{
	struct tca6424_platform_data *pdata = client->dev.platform_data;
	struct tca6424_chip *chip = i2c_get_clientdata(client);
	int ret = 0;

	if (pdata->teardown) {
		ret = pdata->teardown(client, chip->gpio_chip.base,
		chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0) {
			DBGERR(" %s failed, %d\n",__FUNCTION__,ret);
			return ret;
		}
	}

	ret = gpiochip_remove(&chip->gpio_chip);
	if (ret) {
		dev_err(&client->dev, "%s failed, %d\n",
		"gpiochip_remove()", ret);
		return ret;
	}
	kfree(chip);
	return 0;
}

static int tca6424_suspend(struct i2c_client *client, pm_message_t mesg)
{
	DBG("*****************tca6424 suspend*******************");
	return 0;
}

static int tca6424_resume(struct i2c_client *client)
{
	DBG("*****************tca6424 resume*******************");
	return 0;
}

static struct i2c_driver tca6424_driver = {
    .driver = {
		.owner  = THIS_MODULE,
        .name   = "extend_gpio_tca6424",
    },
    .probe      = tca6424_probe,
    .remove     = tca6424_remove,
    .id_table   = tca6424_id,
    .resume = tca6424_resume,
    .suspend = tca6424_suspend,
};
static int __init tca6424_init(void)
{
	int tmp;
	DBG(KERN_ALERT"**********tca6424_init**********\n");
	tmp=i2c_add_driver(&tca6424_driver);
	return 0;
}
subsys_initcall(tca6424_init);

static void __exit tca6424_exit(void)
{
    DBG(KERN_ALERT"**********tca6424_exit**********\n");
	i2c_del_driver(&tca6424_driver);
}
module_exit(tca6424_exit);

MODULE_AUTHOR(" XXX  XXX@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk2818 tca6424 device");
MODULE_LICENSE("GPL");

