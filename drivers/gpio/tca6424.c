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
/*	  COPYRIGHT (C)  ROCK-CHIPS FUZHOU . ALL RIGHTS RESERVED.			  */
/*******************************************************************
FILE			:	tca6424.c
MODIFY		:	sxj
DATE		:	2010-8-11
NOTES		:
********************************************************************/
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

#if 0
#define TCA6424DEB
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#if 1
#define DBGERR(x...)	printk(KERN_INFO x)
#else
#define DBGERR(x...)
#endif

struct tca6424_chip {
	/*  the first extern gpio number in all of gpio groups */
	unsigned gpio_start;
	unsigned gpio_pin_num;
	/*  the first gpio irq  number in all of irq source */
	unsigned gpio_irq_start;
	unsigned irq_pin_num;        			//中断的个数
	unsigned irq_gpiopin;            		//父中断的中断号
	unsigned irq_chain;            			//父中断的中断号
	uint8_t reg_input[TCA6424_PortNum];
	uint8_t reg_output[TCA6424_PortNum];
	uint8_t reg_direction[TCA6424_PortNum];
	uint8_t interrupt_en[TCA6424_PortNum];	// 0 dis
	uint8_t interrupt_mask[TCA6424_PortNum];// 0 unmask
	uint8_t inttype_set[TCA6424_PortNum]; 	// Inttype  enable
	uint8_t inttype[TCA6424_PortNum]; 	
	uint8_t inttype1[TCA6424_PortNum];
	
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
	//struct extgpio_data_s *p;
	struct tca6424_platform_data *dyn_pdata;
	struct work_struct tca6424_work;
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
typedef struct _tca6424_access_{
u8 portreg[TCA6424_PortNum];
u8 accessflag[TCA6424_PortNum];//0 不进行操作
} tca6424_access;

extern inline struct gpio_chip *gpio_to_chip(unsigned gpio);
extern struct lock_class_key gpio_lock_class;
struct workqueue_struct *tca6424workqueue;

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

static int tca6424_gpio_direction_input(struct gpio_chip *gc, uint8_t pin_num)
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
		return ret;
	}
	#endif

	reg_val = tca6424setbit(chip->reg_direction[gpioPortNum], gpioPortPinNum);
	ret = tca6424_write_reg(chip->client, Regaddr, reg_val);
	if(ret<0)
		goto err;

	chip->reg_direction[gpioPortNum] = reg_val;
err:
	
	DBG("**%s[%d],config_reg=%2x,ret=%d**\n",__FUNCTION__,__LINE__,reg_val,ret);

	#ifdef TCA6424_CONFIGREGLOCK
	mutex_unlock(&chip->configreglock);
	#endif

	return (ret<0)?-1:0;
}

static int tca6424_gpio_direction_output(struct gpio_chip *gc,uint8_t pin_num, int val)
{
	struct tca6424_chip *chip;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t reg_val;
	uint8_t Regaddr;
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
	/* then direction */
	reg_val = tca6424clearbit(chip->reg_direction[gpioPortNum], gpioPortPinNum);
	DBG("**%s[%d],reg_val=%2x, Regaddr=%2x,**\n",__FUNCTION__,__LINE__,reg_val,Regaddr);	
	ret = tca6424_write_reg(chip->client, Regaddr, reg_val);
	if(ret<0)
	{
		#ifdef TCA6424_CONFIGREGLOCK
		mutex_unlock(&chip->configreglock);
		#endif
		DBGERR("**%s[%d] set direction reg is error,reg_val=%x,ret=%d**\n",__FUNCTION__,__LINE__,reg_val,ret);
    		return ret;
	}
	chip->reg_direction[gpioPortNum] = reg_val;
	#ifdef TCA6424_CONFIGREGLOCK
	mutex_unlock(&chip->configreglock);
	#endif
    ret=-1;
    #ifdef TCA6424_OUTREGLOCK
	if (!mutex_trylock(&chip->outreglock))
	{
		DBGERR("**%s[%d] Did not get the outreglock**\n",__FUNCTION__,__LINE__);
		return ret;
	}
	#endif
	/* set output level */
	if (val)
		reg_val = tca6424setbit(chip->reg_output[gpioPortNum], gpioPortPinNum);
	else
		reg_val = tca6424clearbit(chip->reg_output[gpioPortNum], gpioPortPinNum);

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
	chip->reg_output[gpioPortNum] = reg_val;

	#ifdef TCA6424_OUTREGLOCK
		mutex_unlock(&chip->outreglock);
	#endif
	
	DBG("**%s[%d],output_reg=%2x,ret=%d**\n",__FUNCTION__,__LINE__,reg_val,ret);	
    return (ret<0)?-1:0;
}

static int tca6424_gpio_get_value(struct gpio_chip *gc, uint8_t pin_num)
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
		return -1;

	Regaddr = TCA6424_InputLevel_Reg+gpioPortNum;	

	if(!tca6424getbit(chip->reg_direction[gpioPortNum],gpioPortPinNum))  //判断该pin是否设置成输出
	{
		DBG("**it is a output pin**\n");
		return -1;
	}
	#ifdef TCA6424_INPUTREGLOCK
	if (!mutex_trylock(&chip->inputreglock))
	{
		DBGERR("**%s[%d]Did not get the inputreglock**\n",__FUNCTION__,__LINE__);
		return -1;
	}
	#endif

	ret = tca6424_read_reg(chip->client, Regaddr, &reg_val);
	if (ret < 0) 
		goto err;
	chip->reg_input[gpioPortNum] = reg_val;
	
err:
	#ifdef TCA6424_CONFIGREGLOCK
		mutex_unlock(&chip->inputreglock);
	#endif
	DBGERR("**%s[%d] input_reg=%2x,ret=%d**\n",__FUNCTION__,__LINE__,reg_val,ret);

	return (ret < 0)?-1:((chip->reg_input[gpioPortNum] >> gpioPortPinNum) & 0x01);
}

static void tca6424_gpio_set_value(struct gpio_chip *gc, uint8_t pin_num, int val)
{
	struct tca6424_chip *chip;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;
	uint8_t reg_val;
	uint8_t Regaddr;
	int ret=-1;
	DBG("**run in the %s**\n",__FUNCTION__);

	chip = container_of(gc, struct tca6424_chip, gpio_chip);

	gpioPortNum = pin_num/8;
	gpioPortPinNum= pin_num%8;

	if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
		return;// -1;

	Regaddr = TCA6424_OutputLevel_Reg+gpioPortNum;	
	
	if(tca6424getbit(chip->reg_direction[gpioPortNum],gpioPortPinNum)) // input state
		return;// -1;

#ifdef TCA6424_OUTREGLOCK
	if (!mutex_trylock(&chip->outreglock))
	{
		DBGERR("**%s[%d] Did not get the outreglock**\n",__FUNCTION__,__LINE__);
		return;// -1;
	}
#endif
	if (val)
		reg_val = tca6424setbit(chip->reg_output[gpioPortNum], gpioPortPinNum);
	else
		reg_val = tca6424clearbit(chip->reg_output[gpioPortNum], gpioPortPinNum);

	ret = tca6424_write_reg(chip->client, Regaddr, reg_val);
	if (ret<0)
		goto err;
	chip->reg_output[gpioPortNum] = reg_val;
	
err: 
	#ifdef TCA6424_OUTREGLOCK
		mutex_unlock(&chip->outreglock);
	#endif
	
	DBG("**%s[%d],output_reg=%2x,ret=%d**\n",__FUNCTION__,__LINE__,reg_val,ret);
	return;// (ret<0)?-1:0;
	
}
static int tca6424_gpio_to_irq(struct gpio_chip *chip,unsigned offset)
{
    struct tca6424_chip *pca_chip = container_of(chip, struct tca6424_chip, gpio_chip);
	if(((pca_chip->gpio_start+offset)>=chip->base)&&((pca_chip->gpio_start+offset)<(chip->base+chip->ngpio)))
	{
  		//DBG("**%s,offset=%d,gpio_irq_start=%d,base=%d,ngpio=%d,gpio_irq_start=%d**\n",
		//	__FUNCTION__,offset,pca_chip->gpio_irq_start,chip->base,chip->ngpio,pca_chip->gpio_irq_start);
		return (offset+pca_chip->gpio_irq_start);
	}
	else
	{
		return -1;
	}
}

int tca6424_checkrange(int start,int num,int val)
{
   
	if((val<(start+num))&&(val>=start))
		return 0;
	else return -1;

}

static void tca6424_gpio_irq_enable(unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	//int gpiopinnum;
	struct tca6424_chip *pchip=(struct tca6424_chip *)desc->chip_data;
	//struct gpio_chip *chip_gpio;
	uint8_t gpio_num;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t tca6424pinnum;


	if(!tca6424_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
	{
		tca6424pinnum = irq-pchip->gpio_irq_start;
	}
	else 
	{
		return;
	}
	gpioPortNum = tca6424pinnum/8;
	gpioPortPinNum= tca6424pinnum%8;
	gpio_num=pchip->gpio_start+tca6424pinnum;
	if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
		return;
	DBG("**%s**\n",__FUNCTION__);
	pchip->interrupt_en[gpioPortNum]=tca6424setbit(pchip->interrupt_en[gpioPortNum],gpioPortPinNum);	
}
static void tca6424_gpio_irq_disable(unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct tca6424_chip *pchip=(struct tca6424_chip *)desc->chip_data;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t tca6424pinnum;

	if(!tca6424_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
	{
		tca6424pinnum=irq-pchip->gpio_irq_start;//irq_to_gpio(irq)
	}
	else 
	{
		return;
	}
	gpioPortNum = tca6424pinnum/8;
	gpioPortPinNum= tca6424pinnum%8;

	if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
		return;
	DBG("**%s**\n",__FUNCTION__);

	pchip->interrupt_en[gpioPortNum]=tca6424clearbit(pchip->interrupt_en[gpioPortNum],gpioPortPinNum);
		
}


static void tca6424_gpio_irq_mask(unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct tca6424_chip *pchip=(struct tca6424_chip *)desc->chip_data;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t tca6424pinnum;

	if(!tca6424_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
	{
		tca6424pinnum=irq-pchip->gpio_irq_start;//irq_to_gpio(irq)
	}
	else 
	{
		return;
	}
	gpioPortNum = tca6424pinnum/8;
	gpioPortPinNum= tca6424pinnum%8;
	
	if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
		return;

	DBG("**%s**\n",__FUNCTION__);

	pchip->interrupt_mask[gpioPortNum]=tca6424setbit(pchip->interrupt_mask[gpioPortNum],gpioPortPinNum);
		
}

static void tca6424_gpio_irq_unmask(unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	//int gpiopinnum;//=irq_to_gpio(irq);
	struct tca6424_chip *pchip=(struct tca6424_chip *)desc->chip_data;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t tca6424pinnum;

	DBG("**%s**\n",__FUNCTION__);

    if(!tca6424_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
    {
		tca6424pinnum=irq-pchip->gpio_irq_start;//irq_to_gpio(irq)
	}
	else 
	{
		return;
	}
	gpioPortNum = tca6424pinnum/8;
	gpioPortPinNum= tca6424pinnum%8;
	
	if((gpioPortNum>=TCA6424_PortNum)||(gpioPortPinNum>=portnum[gpioPortNum]))
		return;
	pchip->interrupt_mask[gpioPortNum]=tca6424clearbit(pchip->interrupt_mask[gpioPortNum],gpioPortPinNum);
}

static int tca6424_gpio_irq_type(unsigned int irq, unsigned int type)
{
	struct irq_desc *desc_irq=irq_to_desc(irq);
	struct tca6424_chip *pchip=(struct tca6424_chip *)desc_irq->chip_data;
	//struct gpio_chip *chip_gpio;
	int gpio_num;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t tca6424pinnum;
	if(!tca6424_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
	{
		tca6424pinnum=irq-pchip->gpio_irq_start;//irq_to_gpio(irq)
		gpio_num=pchip->gpio_start+tca6424pinnum;
	}
	else
		return -1;

	gpioPortNum = tca6424pinnum/8;
	gpioPortPinNum= tca6424pinnum%8;
	 //DBG("**%s %d gpio_num=%d,PortNum=%d,PortPinNum=%d**\n",__FUNCTION__,__LINE__,gpio_num,gpioPortNum,gpioPortPinNum);
	switch (type) {
		case IRQ_TYPE_NONE:
			pchip->inttype_set[gpioPortNum]=tca6424clearbit(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			DBG("**%s IRQ_TYPE_NONE**\n",__FUNCTION__);
			break;
		case IRQ_TYPE_EDGE_RISING:
			pchip->inttype_set[gpioPortNum]=tca6424setbit(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			pchip->inttype[gpioPortNum]=tca6424setbit(pchip->inttype[gpioPortNum],gpioPortPinNum);
			pchip->inttype1[gpioPortNum]=tca6424clearbit(pchip->inttype1[gpioPortNum],gpioPortPinNum);
			DBG("**%s IRQ_TYPE_EDGE_RISING,inttype=%x,inttype1=%x**\n",__FUNCTION__,pchip->inttype[gpioPortNum],pchip->inttype1[gpioPortNum]);

		break;
		case IRQ_TYPE_EDGE_FALLING:
			pchip->inttype_set[gpioPortNum]=tca6424setbit(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			pchip->inttype[gpioPortNum]=tca6424clearbit(pchip->inttype[gpioPortNum],gpioPortPinNum);
			pchip->inttype1[gpioPortNum]=tca6424clearbit(pchip->inttype1[gpioPortNum],gpioPortPinNum);	
			DBG("**%s IRQ_TYPE_EDGE_RISING,inttype=%x,inttype1=%x**\n",__FUNCTION__,pchip->inttype[gpioPortNum],pchip->inttype1[gpioPortNum]);

		break;
		case IRQ_TYPE_EDGE_BOTH:
			pchip->inttype_set[gpioPortNum]=tca6424setbit(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			pchip->inttype1[gpioPortNum]=tca6424setbit(pchip->inttype1[gpioPortNum],gpioPortPinNum);
			DBG("**%s IRQ_TYPE_EDGE_RISING,inttype=%x,inttype1=%x**\n",__FUNCTION__,pchip->inttype[gpioPortNum],pchip->inttype1[gpioPortNum]);
		break;
		case IRQ_TYPE_LEVEL_HIGH:
			pchip->inttype_set[gpioPortNum]=tca6424clearbit(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			DBG("extern gpios does not support IRQ_TYPE_LEVEL_HIGH irq typ");
		break;
		case IRQ_TYPE_LEVEL_LOW:
			pchip->inttype_set[gpioPortNum]=tca6424clearbit(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			DBG("extern gpios does not support IRQ_TYPE_LEVEL_LOW irq typ");
		break;
		default:
		return -EINVAL;
	}
return 0;
}

static int tca6424_gpio_irq_set_wake(unsigned irq, unsigned state)
{
    //no irq wake
	return 0;

}
static struct irq_chip tca6424_gpio_irqchip = {
	.name		= "extend_gpio_tca6424",
	.enable 	= tca6424_gpio_irq_enable,
	.disable	= tca6424_gpio_irq_disable,
	.mask		= tca6424_gpio_irq_mask,
	.unmask		= tca6424_gpio_irq_unmask,
	.set_type	= tca6424_gpio_irq_type,	
	.set_wake	= tca6424_gpio_irq_set_wake,
};


static void tca6424_extend_gpio_irq_handler(struct work_struct *work)
{	
    struct tca6424_chip *pchip = container_of(work, struct tca6424_chip,tca6424_work);
	u8 tempintputreg[TCA6424_PortNum]={0,0,0};
	u8 tempallowint=0;			
	u8 levelchg=0;			
	u8 intbit=0;			
	u8 tempinttype=0;
	int i,j;
	struct irq_desc *gpio_irq_desc;
	unsigned int irq;
	if(tca6424_read_reg(pchip->client,TCA6424_InputLevel_Reg,&tempintputreg[0])<0)
	{
	
		DBG("**%s[%d] reading reg is error\n",__FUNCTION__,__LINE__); 
		enable_irq(pchip->irq_chain);
		return;
	}
	if(tca6424_read_reg(pchip->client,(TCA6424_InputLevel_Reg+1),&tempintputreg[1])<0)
	{
	
		DBG("**%s[%d] reading reg is error\n",__FUNCTION__,__LINE__); 
		enable_irq(pchip->irq_chain);
		return;
	}
	if(tca6424_read_reg(pchip->client,(TCA6424_InputLevel_Reg+2),&tempintputreg[2])<0)
	{
	
		DBG("**%s[%d] reading reg is error\n",__FUNCTION__,__LINE__); 
		enable_irq(pchip->irq_chain);
		return;
	}
	DBG("**has run at %s**,tempintreg[0] = %x,tempintreg[1] = %x,tempintreg[2] = %x\n",__FUNCTION__,tempintputreg[0],tempintputreg[1],tempintputreg[2]);
	if((pchip->interrupt_en[0]==0)&&(pchip->interrupt_en[1]==0)&&(pchip->interrupt_en[2]==0))
	{
		memcpy(&pchip->reg_input[0],&tempintputreg[0],sizeof(tempintputreg));
		DBGERR("there are no pin reg irq\n"); 
		enable_irq(pchip->irq_chain);
		return;
	}

	for(i=0;i<TCA6424_PortNum;i++)
	{
		tempallowint=pchip->interrupt_en[i]&pchip->reg_direction[i]&(~pchip->interrupt_mask[i]);// 满足中断条件
		levelchg=pchip->reg_input[i]^tempintputreg[i];// 找出前后状态不一样的pin
		tempinttype=~(tempintputreg[i]^pchip->inttype[i]);// 找出触发状态和当前pin状态一样的pin，注意只支持low high两种pin触发

	    tempinttype=(~pchip->inttype1[i])&tempinttype;// inttype1 为真的位对应的tempinttype位清零，因为该位只受inttype1控制
 		tempinttype|=pchip->inttype1[i];//电平只要是变化就产生中断
 		tempinttype&=pchip->inttype_set[i];//已经设置了type类型

		intbit=tempallowint&levelchg&tempinttype;
		DBG(" tempallowint=%x,levelchg=%x,tempinttype=%x,intbit=%d\n",tempallowint,levelchg,tempinttype,intbit);

		if(intbit)
		for(j=0;j<portnum[i];j++)
		{
			if(tca6424getbit(intbit,j))
			{
				irq=pchip->gpio_irq_start+TCA6424_PortPinNum*i+j;
				gpio_irq_desc = irq_to_desc(irq);
				gpio_irq_desc->chip->mask(irq);
				generic_handle_irq(irq);
				gpio_irq_desc->chip->unmask(irq);
				DBG("tca6424_i2c_irq_handler port=%d,pin=%d,pinlevel=%d\n",i,j,tca6424getbit(tempintputreg[i],j));
			}
		}
		
		pchip->reg_input[i]=tempintputreg[i];

	}
	enable_irq(pchip->irq_chain);
	return;
}

static irqreturn_t tca6424_gpio_irq_handler(int irq, void * dev_id)
{

	struct irq_desc *gpio_irq_desc = irq_to_desc(irq);
	struct tca6424_chip *pchip=(struct tca6424_chip *)gpio_irq_desc->chip_data;

	DBG("******************%s*******************\n",__FUNCTION__);
	disable_irq_nosync(pchip->irq_chain);
    queue_work(tca6424workqueue,&pchip->tca6424_work);
	return IRQ_HANDLED;
}

static irqreturn_t test_handler(int irq, void * dev_id)
{

	DBG("******************%s*******************\n",__FUNCTION__);
	return IRQ_HANDLED;
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

static void tca6424_gpio_irq_setup(struct tca6424_chip *pchip)
{
    unsigned        pioc, irq_num;
    int ret;
	int testprint1 = 0;
	int testprint2 = 0;
	struct irq_desc *desc;
    irq_num = pchip->gpio_irq_start;   //中断号，扩展io的中断号应该紧跟在内部io中断号的后面。如rk内部中断48个，加上内部gpio 16个虚拟中断，这里pin应该从48+16开始

    for (pioc = 0; pioc < pchip->irq_pin_num; pioc++,irq_num++)
    {
        lockdep_set_class(&irq_desc[irq_num].lock, &gpio_lock_class);
	 /*
         * Can use the "simple" and not "edge" handler since it's
         * shorter, and the AIC handles interrupts sanely.
        */
		testprint1 = set_irq_chip(irq_num, &tca6424_gpio_irqchip);   
		set_irq_handler(irq_num, handle_simple_irq);
		testprint2 =  set_irq_chip_data(irq_num,(void *)pchip);
		desc = irq_to_desc(irq_num);
		DBG("**%s line=%d,test1=%d,test2=%d,desc=%x,chipdate=%x,pchip=%x,irq_num=%d**\n",__FUNCTION__,__LINE__,testprint1,testprint2,desc,desc->chip_data,pchip,irq_num);
		set_irq_flags(irq_num, IRQF_VALID);       
    }
	ret = gpio_request(pchip->irq_gpiopin,NULL);
	if(ret!=0)
	{
		gpio_free(pchip->irq_gpiopin);
		DBG("tca6424_gpio_irq_setup request gpio is err\n");
	}
	
	gpio_pull_updown(pchip->irq_gpiopin, GPIOPullUp);        //gpio 需要拉高irq_to_gpio(pchip->irq_chain)

#if 0

	set_irq_chip_data(pchip->irq_chain, pchip);
	set_irq_chained_handler(pchip->irq_chain, gpio_irq_handlerxxx);
	set_irq_type(pchip->irq_chain,IRQ_TYPE_LEVEL_LOW);
	enable_irq(pchip->irq_chain);

#else
	tca6424workqueue=create_workqueue("tca6424 workqueue");
	INIT_WORK(&pchip->tca6424_work,tca6424_extend_gpio_irq_handler);
	
	set_irq_chip_data(pchip->irq_chain, pchip);
	if(request_irq(pchip->irq_chain,tca6424_gpio_irq_handler, IRQF_TRIGGER_LOW, "tca6424", pchip)!=0)
	{
		DBG("**%s line=%d is err**\n",__FUNCTION__,__LINE__);
	}

#endif
}

int tca6424_init_pintype(struct tca6424_chip *chip,struct i2c_client *client)
{
    int i;
	struct tca6424_platform_data *platform_data=(struct tca6424_platform_data *)client->dev.platform_data;
	struct rk2818_gpio_expander_info *tca6424_gpio_settinginfo;
	uint8_t reg_output[TCA6424_PortNum]={0,0,0};
	uint8_t reg_direction[TCA6424_PortNum]={0,0,0};
	uint8_t tca6424_pin_num;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum,tca6424_settingpin_num=0;

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
						reg_direction[gpioPortNum]=tca6424clearbit(reg_direction[gpioPortNum],gpioPortPinNum);
						if(tca6424_gpio_settinginfo[i].pin_value==GPIO_HIGH)
						{
							reg_output[gpioPortNum]=tca6424setbit(reg_output[gpioPortNum],gpioPortPinNum);
						}
						else
						{
							reg_output[gpioPortNum]=tca6424clearbit(reg_output[gpioPortNum],gpioPortPinNum);
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

	for(i=0; i<TCA6424_PortNum; i++)
	{
	
		if (tca6424_write_reg(client, (TCA6424_Config_Reg+i), reg_direction[i])<0)
		{
			DBGERR("*%s %d* write reg err\n",__FUNCTION__,__LINE__);
			return -1;
		}
		chip->reg_direction[i]=reg_direction[i];
		if (tca6424_write_reg(client, (TCA6424_OutputLevel_Reg+i), reg_output[i])<0)
		{
			DBGERR("*%s %d  write reg err*\n",__FUNCTION__,__LINE__);
			return -1;
		}
		chip->reg_output[i]=reg_output[i];

		if (tca6424_write_reg(client, (TCA6424_Invert_Reg+i), 0)<0)		//make sure this reg be 0
		{
			DBGERR("*%s %d* write reg err\n",__FUNCTION__,__LINE__);
			return -1;
		}
	
		if(tca6424_read_reg(client, (TCA6424_InputLevel_Reg+i), &chip->reg_input[i])<0)
		{
			DBGERR("*%s %d  read reg err*\n",__FUNCTION__,__LINE__);
			return -1;
		}	  
		//DBG("reg_direction=%x,reg_output=%x,reg_input=%x\n",chip->reg_direction[i],chip->reg_output[i],chip->reg_input[i]);

	}

	return 0;
}

static int __devinit tca6424_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	struct tca6424_chip *chip;
	struct tca6424_platform_data  *pdata;
	int ret;
	uint8_t val;
	
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

	client->adapter->dev.platform_data = pdata;
	
	chip->gpio_start = pdata->gpio_base;
	chip->gpio_irq_start =pdata->gpio_irq_start;
	chip->gpio_pin_num=pdata->gpio_pin_num;
	chip->irq_pin_num = pdata->irq_pin_num;
	chip->irq_gpiopin=pdata->tca6424_irq_pin;
	chip->irq_chain = gpio_to_irq(pdata->tca6424_irq_pin);
	chip->names =pdata->names;
	
	//DBG("**%s in %d start=%d,irq_start=%d,pin_num=%d,irq_pin_num=%d,irq_gpiopin=%d,irq_chain=%d,**\n",
	//	__FUNCTION__,__LINE__,chip->gpio_start,chip->gpio_irq_start,chip->gpio_pin_num,chip->irq_pin_num,chip->irq_gpiopin
	//	,chip->irq_chain);

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

	tca6424_gpio_irq_setup(chip);
	i2c_set_clientdata(client, chip);
	chip->client = client;
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


static struct i2c_driver tca6424_driver = {
    .driver = {
		.owner  = THIS_MODULE,
        .name   = "extend_gpio_tca6424",
    },
    .probe      = tca6424_probe,
    .remove     = tca6424_remove,
    .id_table   = tca6424_id,
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

