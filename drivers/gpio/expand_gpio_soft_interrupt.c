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
FILE			:	Soft_interrupt.c
MODIFY		:	sxj
DATE		:	2010-9-2
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


#define EXTPAND_GPIO_GET_BIT(a,num) (((a)>>(num))&0x01)
#define EXTPAND_GPIO_SET_BIT(a,num) ((a)|(0x01<<(num)))
#define EXTPAND_GPIO_CLEAR_BIT(a,num) ((a)&(~(0x01<<(num))))
#define MIN(x,y) (((x)<(y))?(x):(y)) 


static int expand_gpio_irq_en = -1;
static int expand_gpio_irq_ctrflag = 0;
static unsigned int expand_gpio_irq_num = 0;

static struct workqueue_struct *irqworkqueue;
static struct lock_class_key gpio_lock_class;

struct expand_gpio_soft_int expand_irq_data;

void expand_gpio_irq_ctr_dis(int irq,int ctrflag)
{
		expand_gpio_irq_ctrflag=0;
		if(expand_gpio_irq_en)
		{	
	 		expand_gpio_irq_en=0;
	 		disable_irq_nosync(irq);
			DBG("***********%s %d***********\n",__FUNCTION__,__LINE__);
		}
		if(ctrflag)
		{
			expand_gpio_irq_ctrflag=-1;
		}
}

void expand_gpio_irq_ctr_en(int irq)
{	
		if(!expand_gpio_irq_en)
		{	
		     DBG("***********%s %d***********\n",__FUNCTION__,__LINE__);
			 expand_gpio_irq_en = -1;
			 enable_irq(irq);
		}
}

static int expand_checkrange(int start,int num,int val)
{
   
	if((val<(start+num))&&(val>=start))
	{
		return 0;
	}
	else 
	{
		return -1;
	}

}

static void expand_gpio_irq_enable(unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct expand_gpio_soft_int *pchip=(struct expand_gpio_soft_int *)desc->chip_data;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t expandpinnum;

	if(!expand_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
	{
		expandpinnum = irq - pchip->gpio_irq_start;//irq_to_gpio(irq)
	}
	else 
	{
		return;
	}
	gpioPortNum = expandpinnum/(pchip->expand_port_pinnum);
	gpioPortPinNum= expandpinnum%(pchip->expand_port_pinnum);

	if((gpioPortNum>=(pchip->expand_port_group))||(gpioPortPinNum>=(pchip->expand_port_pinnum)))
		return;
	//DBG("**%s**\n",__FUNCTION__);
	pchip->interrupt_en[gpioPortNum]=EXTPAND_GPIO_SET_BIT(pchip->interrupt_en[gpioPortNum],gpioPortPinNum);	
}
static void expand_gpio_irq_disable(unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct expand_gpio_soft_int *pchip=(struct expand_gpio_soft_int *)desc->chip_data;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t expandpinnum;

	if(!expand_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
	{
		expandpinnum=irq - pchip->gpio_irq_start;//irq_to_gpio(irq)
	}
	else 
	{
		return;
	}
	gpioPortNum = expandpinnum/(pchip->expand_port_pinnum);
	gpioPortPinNum= expandpinnum%(pchip->expand_port_pinnum);

	if((gpioPortNum>=(pchip->expand_port_group))||(gpioPortPinNum>=(pchip->expand_port_pinnum)))
		return;
	//DBG("**%s**\n",__FUNCTION__);
	pchip->interrupt_en[gpioPortNum]=EXTPAND_GPIO_CLEAR_BIT(pchip->interrupt_en[gpioPortNum],gpioPortPinNum);
		
}

static void expand_gpio_irq_mask(unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct expand_gpio_soft_int *pchip=(struct expand_gpio_soft_int *)desc->chip_data;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t expandpinnum;

	if(!expand_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
	{
		expandpinnum=irq-pchip->gpio_irq_start;//irq_to_gpio(irq)
	}
	else 
	{
		return;
	}
	gpioPortNum = expandpinnum/(pchip->expand_port_pinnum);
	gpioPortPinNum= expandpinnum%(pchip->expand_port_pinnum);
	if((gpioPortNum>=(pchip->expand_port_group))||(gpioPortPinNum>=(pchip->expand_port_pinnum)))
		return;
	//DBG("**%s**\n",__FUNCTION__);
	pchip->interrupt_mask[gpioPortNum]=EXTPAND_GPIO_SET_BIT(pchip->interrupt_mask[gpioPortNum],gpioPortPinNum);	
}

static void expand_gpio_irq_unmask(unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct expand_gpio_soft_int *pchip=(struct expand_gpio_soft_int *)desc->chip_data;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t expandpinnum;

    if(!expand_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
    {
		expandpinnum=irq-pchip->gpio_irq_start;//irq_to_gpio(irq)
	}
	else 
	{
		return;
	}
	gpioPortNum = expandpinnum/(pchip->expand_port_pinnum);
	gpioPortPinNum= expandpinnum%(pchip->expand_port_pinnum);
	if((gpioPortNum>=(pchip->expand_port_group))||(gpioPortPinNum>=(pchip->expand_port_pinnum)))
		return;
	//DBG("**%s**\n",__FUNCTION__);
	pchip->interrupt_mask[gpioPortNum]=EXTPAND_GPIO_CLEAR_BIT(pchip->interrupt_mask[gpioPortNum],gpioPortPinNum);
}

static int expand_gpio_irq_type(unsigned int irq, unsigned int type)
{
	struct irq_desc *desc_irq=irq_to_desc(irq);
	struct expand_gpio_soft_int *pchip=(struct expand_gpio_soft_int *)desc_irq->chip_data;
	uint8_t gpioPortNum;
	uint8_t gpioPortPinNum;    
	uint8_t expandpinnum;
	if(!expand_checkrange(pchip->gpio_irq_start,pchip->irq_pin_num,irq))
	{
		expandpinnum = irq - pchip->gpio_irq_start;//irq_to_gpio(irq)
	}
	else
	{
		return -1;
	}

	gpioPortNum = expandpinnum/(pchip->expand_port_pinnum);
	gpioPortPinNum= expandpinnum%(pchip->expand_port_pinnum);
	if((gpioPortNum>=(pchip->expand_port_group))||(gpioPortPinNum>=(pchip->expand_port_pinnum)))
		return -1;
	DBG("**%s %d PortNum=%d,PortPinNum=%d**\n",__FUNCTION__,__LINE__,gpioPortNum,gpioPortPinNum);
	switch (type) {
		case IRQ_TYPE_NONE:
			pchip->inttype_set[gpioPortNum]=EXTPAND_GPIO_CLEAR_BIT(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			DBG("**%s IRQ_TYPE_NONE**\n",__FUNCTION__);
			break;
		case IRQ_TYPE_EDGE_RISING:
			pchip->inttype_set[gpioPortNum]=EXTPAND_GPIO_SET_BIT(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			pchip->inttype[gpioPortNum]=EXTPAND_GPIO_SET_BIT(pchip->inttype[gpioPortNum],gpioPortPinNum);
			pchip->inttype1[gpioPortNum]=EXTPAND_GPIO_CLEAR_BIT(pchip->inttype1[gpioPortNum],gpioPortPinNum);
			DBG("**%s IRQ_TYPE_EDGE_RISING,inttype=%x,inttype1=%x**\n",__FUNCTION__,pchip->inttype[gpioPortNum],pchip->inttype1[gpioPortNum]);
			break;
		case IRQ_TYPE_EDGE_FALLING:
			pchip->inttype_set[gpioPortNum]=EXTPAND_GPIO_SET_BIT(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			pchip->inttype[gpioPortNum]=EXTPAND_GPIO_CLEAR_BIT(pchip->inttype[gpioPortNum],gpioPortPinNum);
			pchip->inttype1[gpioPortNum]=EXTPAND_GPIO_CLEAR_BIT(pchip->inttype1[gpioPortNum],gpioPortPinNum);	
			DBG("**%s IRQ_TYPE_EDGE_RISING,inttype=%x,inttype1=%x**\n",__FUNCTION__,pchip->inttype[gpioPortNum],pchip->inttype1[gpioPortNum]);
			break;
		case IRQ_TYPE_EDGE_BOTH:
			pchip->inttype_set[gpioPortNum]=EXTPAND_GPIO_SET_BIT(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			pchip->inttype1[gpioPortNum]=EXTPAND_GPIO_SET_BIT(pchip->inttype1[gpioPortNum],gpioPortPinNum);
			DBG("**%s IRQ_TYPE_EDGE_RISING,inttype=%x,inttype1=%x**\n",__FUNCTION__,pchip->inttype[gpioPortNum],pchip->inttype1[gpioPortNum]);
			break;
		case IRQ_TYPE_LEVEL_HIGH:
			pchip->inttype_set[gpioPortNum]=EXTPAND_GPIO_CLEAR_BIT(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			DBG("extern gpios does not support IRQ_TYPE_LEVEL_HIGH irq typ");
			break;
		case IRQ_TYPE_LEVEL_LOW:
			pchip->inttype_set[gpioPortNum]=EXTPAND_GPIO_CLEAR_BIT(pchip->inttype_set[gpioPortNum],gpioPortPinNum);
			DBG("extern gpios does not support IRQ_TYPE_LEVEL_LOW irq typ");
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int expand_gpio_irq_set_wake(unsigned irq, unsigned state)
{
    //no irq wake
	return 0;
}
static struct irq_chip expand_gpio_irqchip = {
	.name		= "expand_gpio_expand ",
	.enable 	= expand_gpio_irq_enable,
	.disable	= expand_gpio_irq_disable,
	.mask		= expand_gpio_irq_mask,
	.unmask		= expand_gpio_irq_unmask,
	.set_type	= expand_gpio_irq_type,	
	.set_wake	= expand_gpio_irq_set_wake,
};

static irqreturn_t expand_gpio_irq_handler(int irq, void * dev_id)
{
	struct irq_desc *gpio_irq_desc = irq_to_desc(irq);
	struct expand_gpio_soft_int *pchip=(struct expand_gpio_soft_int *)gpio_irq_desc->chip_data;
	u8 oldintputreg[MAX_SUPPORT_PORT_GROUP]={0,0,0,0,0};
	u8 tempintputreg[MAX_SUPPORT_PORT_GROUP]={0,0,0,0,0};
	u8 tempallowint=0;			
	u8 levelchg=0;			
	u8 intbit[MAX_SUPPORT_PORT_GROUP]={0,0,0,0,0};			
	u8 tempinttype=0;
	u8 int_en_flag=0;
	int i,j;

	DBG("******************%s*******************\n",__FUNCTION__);
	expand_gpio_irq_ctr_dis(pchip->irq_chain,0);
	memcpy(&oldintputreg[0],&pchip->gvar->reg_input[0],pchip->expand_port_group);
	if(pchip->irq_data.read_allinputreg(pchip->irq_data.data,&tempintputreg[0]))
	{
		expand_gpio_irq_ctr_dis(pchip->irq_chain,-1);
		DBG("**%s[%d] reading reg is error\n",__FUNCTION__,__LINE__); 
		queue_work(irqworkqueue,&pchip->irq_work);
		return IRQ_HANDLED;
	}
	
	memcpy(&pchip->gvar->reg_input[0],&tempintputreg[0],pchip->expand_port_group);
	//DBG("**has run at %s**,input[0] = %x,input[1] = %x,input[2] = %x\n",__FUNCTION__,pchip->gvar.reg_input[0],pchip->gvar.reg_input[1],pchip->gvar.reg_input[2]);

	//Handle for different expand_port_group 
    for(i=0,int_en_flag=0;i<MIN(pchip->expand_port_group,MAX_SUPPORT_PORT_GROUP);i++)
    {
		int_en_flag|=pchip->interrupt_en[i];
    }

	if(!int_en_flag)
	{		
		if(expand_gpio_irq_num<0xFFFFFFFF)
		{
			expand_gpio_irq_num++;
		}
		else
		{
			expand_gpio_irq_num=0;
		}
		DBGERR("there are no pin reg irq\n"); 
		expand_gpio_irq_ctr_en(pchip->irq_chain);
		return IRQ_HANDLED;
	}

	for(i=0;i<pchip->expand_port_group;i++)
	{
		tempallowint=pchip->interrupt_en[i]&pchip->gvar->reg_direction[i]&(~pchip->interrupt_mask[i]);// 满足中断条件
		levelchg=oldintputreg[i]^tempintputreg[i];// 找出前后状态不一样的pin
		tempinttype=~(tempintputreg[i]^pchip->inttype[i]);// 找出触发状态和当前pin状态一样的pin，注意只支持low high两种pin触发

	    tempinttype=(~pchip->inttype1[i])&tempinttype;// inttype1 为真的位对应的tempinttype位清零，因为该位只受inttype1控制
 		tempinttype|=pchip->inttype1[i];//电平只要是变化就产生中断
 		tempinttype&=pchip->inttype_set[i];//已经设置了type类型

		intbit[i]=tempallowint&levelchg&tempinttype;
		//DBG(" tempallowint=%x,levelchg=%x,tempinttype=%x,intbit=%d\n",tempallowint,levelchg,tempinttype,intbit[i]);
	}
	if(expand_gpio_irq_num<0xFFFFFFFF)
	{
		expand_gpio_irq_num++;
	}
	else
	{
		expand_gpio_irq_num=0;
	}
	for(i=0;i<pchip->expand_port_group;i++)
	{
		if(intbit[i])
		{
			for(j=0;j<pchip->expand_port_pinnum;j++)
			{
				if(EXTPAND_GPIO_GET_BIT(intbit[i],j))
				{
					irq=pchip->gpio_irq_start+pchip->expand_port_pinnum*i+j;
					gpio_irq_desc = irq_to_desc(irq);
					gpio_irq_desc->chip->mask(irq);
					generic_handle_irq(irq);
					gpio_irq_desc->chip->unmask(irq);
					//DBG("expand_i2c_irq_handler port=%d,pin=%d,pinlevel=%d\n",i,j,EXTPAND_GPIO_GET_BIT(tempintputreg[i],j));
				}
			}
		}
	}
	expand_gpio_irq_ctr_en(pchip->irq_chain);
	return IRQ_HANDLED;
}

static void irq_call_back_handler(struct work_struct *work)
{
	struct expand_gpio_soft_int *pchip = container_of(work, struct expand_gpio_soft_int,irq_work);
	//printk("irq_call_back_handle\n");
	expand_gpio_irq_handler(pchip->irq_chain,NULL);
}

void expand_gpio_irq_setup(struct expand_gpio_soft_int *pchip)
{
    unsigned int pioc, irq_num;
    int ret;
	struct irq_desc *desc;
    irq_num = pchip->gpio_irq_start;   //中断号，扩展io的中断号应该紧跟在内部io中断号的后面。如rk内部中断48个，加上内部gpio 16个虚拟中断，这里pin应该从48+16开始

	DBG("**%s**\n",__FUNCTION__);
    for (pioc = 0; pioc < pchip->irq_pin_num; pioc++,irq_num++)
    {
        lockdep_set_class(&irq_desc[irq_num].lock, &gpio_lock_class);
	 /*
         * Can use the "simple" and not "edge" handler since it's
         * shorter, and the AIC handles interrupts sanely.
        */
		set_irq_chip(irq_num, &expand_gpio_irqchip);   
		set_irq_handler(irq_num, handle_simple_irq);
		set_irq_chip_data(irq_num,(void *)pchip);
		desc = irq_to_desc(irq_num);
		DBG("**%s line=%d,irq_num=%d**\n",__FUNCTION__,__LINE__,irq_num);
		set_irq_flags(irq_num, IRQF_VALID);       
    }
	ret = gpio_request(pchip->irq_gpiopin,NULL);
	if(ret!=0)
	{
		gpio_free(pchip->irq_gpiopin);
		DBG("expand_gpio_irq_setup request gpio is err\n");
	}
	gpio_pull_updown(pchip->irq_gpiopin, pchip->rk_irq_gpio_pull_up_down);        //gpio 需要拉高irq_to_gpio(pchip->irq_chain)
	irqworkqueue=create_rt_workqueue("irq workqueue");
	INIT_WORK(&pchip->irq_work,irq_call_back_handler);
	set_irq_chip_data(pchip->irq_chain, pchip);
	if(request_irq(pchip->irq_chain,expand_gpio_irq_handler,pchip->rk_irq_mode, "expand", pchip)!=0)
	{
		DBG("**%s line=%d is err**\n",__FUNCTION__,__LINE__);
	}
}

int wait_untill_input_reg_flash(void)
{
	unsigned int num = 0;
    	unsigned int tempnum = expand_gpio_irq_num;

	while(expand_gpio_irq_ctrflag&&(expand_gpio_irq_num==tempnum))
	{
		mdelay(1);
		num++;
		if(num>5)
			return -1;
	}
	return 0;
}

void expand_irq_init(void *data,struct expand_gpio_global_variable *var,irq_read_inputreg handler)
{
	expand_irq_data.irq_data.data = data;
	expand_irq_data.irq_data.read_allinputreg = handler;
	expand_irq_data.gvar = var;
	expand_gpio_irq_setup(&expand_irq_data);
}







