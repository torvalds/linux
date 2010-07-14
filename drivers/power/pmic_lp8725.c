/* arch/arm/mach-rk2818/example.c
 *
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
 *
 */
/*******************************************************************/
/*	  COPYRIGHT (C)  ROCK-CHIPS FUZHOU . ALL RIGHTS RESERVED.			  */
/*******************************************************************
FILE		:	  PCA9554.C
DESC		:	  扩展GPIO 的驱动相关程序
AUTHOR		:	  ZHONGYW  
DATE		:	  2009-4-26
NOTES		:
$LOG: GPIO.C,V $
REVISION 0.01
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


#if 1
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#if 1
#define DBGERR(x...)	printk(KERN_INFO x)
#else
#define DBGERR(x...)
#endif

#define DEFSEL_VIN1

#define GENERAL_REG_ADDR 0x0
#define LDO1_REG_ADDR 0x01
#define LDO2_REG_ADDR 0x02
#define LDO3_REG_ADDR 0x03
#define LDO4_REG_ADDR 0x04
#define LDO5_REG_ADDR 0x05
#define LILO1_REG_ADDR 0x06
#define LILO2_REG_ADDR 0x07
#define BUCK1_V1_REG_ADDR 0x08
#define BUCK1_V2_REG_ADDR 0x09
#define BUCK2_V1_REG_ADDR 0x0a
#define BUCK2_V2_REG_ADDR 0x0b
#define BUCK_CTR_REG_ADDR 0x0c
#define LDO_CTR_REG_ADDR 0x0d
#define PULLDOWN_REG_ADDR 0x0e
#define STATUS_REG_ADDR 0x0f

#define LP8725_LDO_NUM 5
#define LP8725_LILO_NUM 2
#define LP8725_BUCK_NUM 2


#define LDO_BUCKV1_TIME 5 //高3 
#define LDO_BUCKV_V 0 //第5
#define BUCKV2_CL_BIT 6 //高2

#ifdef DEFSEL_VIN1
#define GENERAL_REG_DEFAULT 0x59 //0101 1001     buck1output  dependon dvs pin, dvs=1 v1 is used               buck2v1 is output

#define LDO1_REG_T_DEFAULT 0x02 //1001 1001  
#define LDO1_REG_V_DEFAULT 0x19
#define LDO2_REG_T_DEFAULT 0x04 //1001 1001
#define LDO2_REG_V_DEFAULT 0x19
#define LDO3_REG_T_DEFAULT 0x05 // 1011 1111
#define LDO3_REG_V_DEFAULT 0x1f
#define LDO4_REG_T_DEFAULT 0x04 // 1001 1101
#define LDO4_REG_V_DEFAULT 0x1d 
#define LDO5_REG_T_DEFAULT 0x04 //1000 1100
#define LDO5_REG_V_DEFAULT 0x0c

#define LILO1_REG_T_DEFAULT 0x05 //1010 1000
#define LILO1_REG_V_DEFAULT 0x08
#define LILO2_REG_T_DEFAULT 0x02  // 0101 0000 
#define LILO2_REG_V_DEFAULT 0x10

#define BUCK1_V1_REG_T_DEFAULT 0x00 // 0000 0100
#define BUCK1_V1_REG_V_DEFAULT 0x04

#define BUCK1_V2_REG_C_DEFAULT 0x03 //1100 1000 高二
#define BUCK1_V2_REG_V_DEFAULT 0x08 

#define BUCK2_V1_REG_T_DEFAULT 0x02  // 0101 0001
#define BUCK2_V1_REG_V_DEFAULT 0x11

#define BUCK2_V2_REG_C_DEFAULT 0x02//1001 0001  高二
#define BUCK2_V2_REG_V_DEFAULT 0x11// 第5

#define BUCK2_CTR_REG_ADDR 0x11 // 0001 0001
#define LDO_CTR_REG_DEFAULT 0x7f //0111 1111
#define PULLDOWN_DEFAULT 0x7f //0111 1111
#define STATUS_REG_DEFAULT 0x00 //0000 0000

#else



#define GENERAL_REG_DEFAULT 0x59 //0101 1001

#define LDO1_REG_T_DEFAULT 0x01 // 0011 0101  
#define LDO1_REG_V_DEFAULT 0x15
#define LDO2_REG_T_DEFAULT 0x01 // 0011 1001
#define LDO2_REG_V_DEFAULT 0x19

#define LDO3_REG_T_DEFAULT 0x02 // 0101 1001
#define LDO3_REG_V_DEFAULT 0x19
#define LDO4_REG_T_DEFAULT 0x02 // 0101 1001
#define LDO4_REG_V_DEFAULT 0x19 
#define LDO5_REG_T_DEFAULT 0x01 // 0011 1001
#define LDO5_REG_V_DEFAULT 0x19

#define LILO1_REG_T_DEFAULT 0x01 //0011 1111
#define LILO1_REG_V_DEFAULT 0x1f

#define LILO2_REG_T_DEFAULT 0x00  // 0000 1000
#define LILO2_REG_V_DEFAULT 0x08

#define BUCK1_V1_REG_T_DEFAULT 0x00 // 0000 1000
#define BUCK1_V1_REG_V_DEFAULT 0x08

#define BUCK1_V2_REG_C_DEFAULT 0x03 //1100 0100 高二
#define BUCK1_V2_REG_V_DEFAULT 0x04 

#define BUCK2_V1_REG_T_DEFAULT 0x01  // 0011 0001
#define BUCK2_V1_REG_V_DEFAULT 0x11

#define BUCK2_V2_REG_C_DEFAULT 0x02 //1001 0001  高二
#define BUCK2_V2_REG_V_DEFAULT 0x11// 第5

#define BUCK_CTR_REG_ADDR 0x11 // 0001 0001
#define LDO_CTR_REG_DEFAULT 0x7f //0111 1111
#define PULLDOWN_REG_DEFAULT 0x7f //0111 1111
#define STATUS_REG_DEFAULT 0x00 //0000 0000
#endif


struct i2c_client *lp8725_client;
#define lp8725getbit(a,num) (((a)>>(num))&0x01)
#define lp8725setbit(a,num) ((a)|(0x01<<(num)))
#define lp8725clearbit(a,num) ((a)&(~(0x01<<(num))))


static const struct i2c_device_id lp8725_id[] = 
{
     { "lp8725", 0, }, 
    { }
};

MODULE_DEVICE_TABLE(i2c, lp8725_id);

struct lp8725_regs_s
{
uint8_t general_reg_val;
uint8_t ldo_reg_val[5];
uint8_t lilo_reg_val[2];
uint8_t buck_v1_reg_val[2];	
uint8_t buck_v2_reg_val[2];	
uint8_t buck_ctr_reg_val;	
uint8_t ldo_ctr_reg_val;	
uint8_t pulldown_reg_val;	
uint8_t status_reg_val;	
};
struct lp8725_vol_range_s
{
  uint16_t vol_low;
  uint16_t vol_high;
  uint16_t rang;
};


static struct lp8725_regs_s lp8725_regs;


uint8_t ldo_reg_t_default[LP8725_LDO_NUM]={LDO1_REG_T_DEFAULT,LDO2_REG_T_DEFAULT,LDO3_REG_T_DEFAULT,LDO4_REG_T_DEFAULT,LDO5_REG_T_DEFAULT};
uint8_t ldo_reg_v_default[LP8725_LDO_NUM]={LDO1_REG_V_DEFAULT,LDO2_REG_V_DEFAULT,LDO3_REG_V_DEFAULT,LDO4_REG_V_DEFAULT,LDO5_REG_V_DEFAULT};

uint8_t lilo_reg_t_default[LP8725_LILO_NUM]={LILO1_REG_T_DEFAULT,LILO2_REG_T_DEFAULT};
uint8_t lilo_reg_v_default[LP8725_LILO_NUM]={LILO1_REG_V_DEFAULT,LILO2_REG_V_DEFAULT};

uint8_t buck_v1_reg_t_default[LP8725_BUCK_NUM]={BUCK1_V1_REG_T_DEFAULT,BUCK2_V1_REG_T_DEFAULT};
uint8_t buck_v1_reg_v_default[LP8725_BUCK_NUM]={BUCK1_V1_REG_V_DEFAULT,BUCK2_V1_REG_V_DEFAULT};

uint8_t buck_v2_reg_c_default[LP8725_BUCK_NUM]={BUCK1_V2_REG_C_DEFAULT,BUCK2_V2_REG_C_DEFAULT};
uint8_t buck_v2_reg_v_default[LP8725_BUCK_NUM]={BUCK1_V2_REG_V_DEFAULT,BUCK2_V2_REG_V_DEFAULT};


#define LDO_RANG 5
#define LILO_RANG 5
#define BULK_RANG 6

struct lp8725_vol_range_s ldo_vol_range[LDO_RANG]={{120,190,5},{190,260,10},{260,300,5},{300,310,10},{310,330,20}};
struct lp8725_vol_range_s lilo_vol_range[LILO_RANG]={{80,140,5},{140,280,10},{280,290,5},{290,310,10},{310,330,20}};
struct lp8725_vol_range_s buck_vol_range[BULK_RANG]={{80,140,5},{140,170,10},{170,190,5},{190,280,10},{280,290,5},{290,300,10}};
/* **********************************************************************
 *  写寄存器
 * return    <0 failed
 * * ***********************************************************************/
static int lp8725_write_reg(struct i2c_client *client, uint8_t reg, uint8_t val)
{
	int ret=-1;
	DBG("**run in %s**\n",__FUNCTION__);
	struct i2c_adapter *adap;
	struct i2c_msg msg;
	char tx_buf[2];
	if(!client)
		return ret;    
	adap = client->adapter;		
	tx_buf[0] = reg;
	tx_buf[1]=val;
	//DBG("**run in %s**\n",__FUNCTION__);

	msg.addr = client->addr;
	msg.buf = &tx_buf[0];
	msg.len = 1 +1;
	msg.flags = client->flags;   
	msg.scl_rate = 200*1000;
	//DBG("**run in %s**\n",__FUNCTION__);

	ret = i2c_transfer(adap, &msg, 1);
	//DBG("**run in %s**\n",__FUNCTION__);
	return ret;    
	DBG("**run out %s**\n",__FUNCTION__);
}
/* **********************************************************************
 *  读寄存器
 * return    <0 failed
 * * ***********************************************************************/
 #if 1
static int lp8725_read_reg(struct i2c_client *client, uint8_t reg, uint8_t *val)
{

    int ret;
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
    msgs[0].scl_rate = 200*1000;
    //接收数据
    //msgs[1].buf = val;
    DBG("msgs[1].buf = %d\n",*msgs[1].buf);
	//msgs[1].buf = val;	
	msgs[1].buf = val;
    msgs[1].addr = client->addr;
    msgs[1].flags = client->flags | I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].scl_rate = 200*1000;

    ret = i2c_transfer(adap, msgs, 2);
    //DBG("**has run at %s %d ret=%d**\n",__FUNCTION__,__LINE__,ret);
	DBG("msgs[1].buf = %d\n",*(msgs[1].buf));

    return ret;     
}
#else
static int lp8725_read_reg(struct i2c_client *client, uint8_t reg)
{

  int ret;
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
	msgs[0].scl_rate = 200*1000;
	//接收数据
	msgs[1].buf;
	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].scl_rate = 200*1000;

	ret = i2c_transfer(adap, msgs, 2);
	//DBG("**has run at %s %d ret=%d**\n",__FUNCTION__,__LINE__,ret);
	DBG("msgs[1].buf = %d\n",*msgs[1].buf);
	return ret; 	
}
#endif
/* **********************************************************************
 *  设置ldo的输出电压
 *ldon:ldo编号 如ldo1 ldon=1，vol 电压单位10mv    如1.2v  vol=120
 *电压范围:从1.2v开始步进0.05v
 * return    <0 failed
 * * ***********************************************************************/
int lp8725_set_ldo_vol(uint8_t ldon,uint16_t vol)
{

	uint8_t val=0;
	uint8_t reg_val=0;
	int i;

	if((vol>ldo_vol_range[LDO_RANG-1].vol_high)||(vol<ldo_vol_range[0].vol_low)||ldon>LP8725_LDO_NUM)
		return -1;
	
	for(i=0;i<LDO_RANG;i++)
	{
		if(vol<=ldo_vol_range[i].vol_high)
		{
			val+=(vol-ldo_vol_range[i].vol_low)/ldo_vol_range[i].rang;
			break;
		}
		else
			val+=(ldo_vol_range[i].vol_high-ldo_vol_range[i].vol_low)/ldo_vol_range[i].rang;
		DBG("val=0x%x,vol=%d\n",val,vol);
	}
	
	DBG("val=0x%x,vol=%d\n",val,vol);

	reg_val=(ldo_reg_t_default[ldon-1]<<LDO_BUCKV1_TIME)|val;
	DBG("ldo_reg_t_default[ldon-1]=0x%x,LDO_BUCKV1_TIME=0x%x\n",ldo_reg_t_default[ldon-1],LDO_BUCKV1_TIME);
	DBG("ldo_reg_t_default[ldon-1]<<LDO_BUCKV1_TIME=0x%x\n",ldo_reg_t_default[ldon-1]<<LDO_BUCKV1_TIME);
	DBG("ADDR=0x%x,reg_val=0x%x\n",LDO1_REG_ADDR+ldon-1,reg_val);

	if(lp8725_write_reg(lp8725_client,LDO1_REG_ADDR+ldon-1,reg_val)<0)
	{
		DBGERR("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}
	
	lp8725_regs.ldo_reg_val[ldon-1]=reg_val;
	return 0;
}

/* **********************************************************************
 *  设置lilo的输出电压
 *ldon:ldo编号 如lilo1 lilon=1，vol 电压单位10mv    如0.8v  vol=80
 *电压范围:电压在[0.8,1.4]时步进0.05v，电压在[1.4,3.3]时步进0.1v
 * return    <0 failed
 * * ***********************************************************************/
int lp8725_set_lilo_vol(uint8_t lilon,uint16_t vol)
{

	uint8_t val=0;
	uint8_t reg_val=0;
	int i;

	if((vol>lilo_vol_range[LILO_RANG-1].vol_high)||(vol<lilo_vol_range[0].vol_low)||lilon>LP8725_LILO_NUM)
	return -1;

	for(i=0;i<LILO_RANG;i++)
	{
		if(vol<=lilo_vol_range[i].vol_high)
		{
			val+=(vol-lilo_vol_range[i].vol_low)/lilo_vol_range[i].rang;
			break;
		}
		else
			val+=(lilo_vol_range[i].vol_high-lilo_vol_range[i].vol_low)/lilo_vol_range[i].rang;
	}
	
	reg_val=(lilo_reg_t_default[lilon-1]<<LDO_BUCKV1_TIME)|val;
	
	if(lp8725_write_reg(lp8725_client,LILO1_REG_ADDR+lilon-1,reg_val)<0)
	{
		DBGERR("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}
	
	lp8725_regs.lilo_reg_val[lilon-1]=reg_val;
	
	return 0;
}


/* **********************************************************************
*  设置buck的v1 v2  输出电压控制寄存器
*ldon:ldo编号 如buck1 buckn=1，vol 电压单位10mv    如0.8v  vol=80 ，v2=1 将电压值写入v2 reg，否则写入v1 reg
*电压范围:电压在[0.8,1.4]时步进0.05v，电压在[1.4,1.7]时步进0.1v，
*电压在[1.7,1.9]时步进0.05v，电压在[1.9,3.0]时步进0.1v
* return    <0 failed
* * ***********************************************************************/
static int lp8725_set_buck_vol_v1orv2(uint8_t buckn,uint16_t vol,uint8_t v2)
{

	uint8_t val=0;
	uint8_t reg_val=0;
	uint8_t reg_addr=0;
	int i;

	if(vol>buck_vol_range[BULK_RANG-1].vol_high||vol<buck_vol_range[0].vol_low||buckn>LP8725_BUCK_NUM)
		return -1;

	for(i=0;i<BULK_RANG;i++)
	{
		if(vol<=buck_vol_range[i].vol_high)
		{
			val+=(vol-buck_vol_range[i].vol_low)/buck_vol_range[i].rang;
			break;
		}
		else
			val+=(buck_vol_range[i].vol_high-buck_vol_range[i].vol_low)/buck_vol_range[i].rang;
	}

	reg_addr=BUCK1_V1_REG_ADDR+(buckn-1)*2;
	reg_val=(buck_v1_reg_t_default[buckn-1]<<LDO_BUCKV1_TIME)|val;

	if(v2)
		reg_addr=BUCK1_V2_REG_ADDR+(buckn-1)*2;


	if(lp8725_write_reg(lp8725_client,reg_addr,reg_val)<0)
	{
		DBGERR("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}

	if(v2)
		lp8725_regs.buck_v2_reg_val[buckn-1]=reg_val;
	else
		lp8725_regs.buck_v1_reg_val[buckn-1]=reg_val;

	return 0;
}
/***************************************************************
*
*
****************************************************************/
int lp8725_set_buck_dvsn_v_bit(uint8_t buckn,uint8_t dvs)
{


	uint8_t reg_val=0;

	if(dvs)
	reg_val=lp8725_regs.general_reg_val|(0x01<<(2+buckn-1));
	else
	reg_val=lp8725_regs.general_reg_val&(~0x01<<(12+buckn-1));

	   
	if(lp8725_write_reg(lp8725_client,GENERAL_REG_ADDR,reg_val)<0)
	{

		DBGERR("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}

	lp8725_regs.general_reg_val=reg_val;

	return 0;
}
/* **********************************************************************
*  设置buck的输出电压
*ldon:ldo编号 如buck1 buckn=1，vol 电压单位10mv    如0.8v  vol=80
*电压范围:电压在[0.8,1.4]时步进0.05v，电压在[1.4,1.7]时步进0.1v，
*电压在[1.7,1.9]时步进0.05v，电压在[1.9,3.0]时步进0.1v
*每个buck 有两电压寄存器v1、v2，buck 的输出电压根据一定条件，
*选择根据那个寄存器的值输出电压
* buck1: dvs1_v=1 output vol depend on  buck1_v1 reg,else depend on dvs pin. dvspin=1 output vol depend on  buck1_v1,esle output vol depend on  buck1_v2
*buck2:dvs1_v=1 output vol depend on  buck2_v1 reg,else depend on buck2_v2. 
* return    <0 failed
* * ***********************************************************************/

 int lp8725_set_buck_vol(uint8_t buckn,uint16_t vol)
 {
       
	if(lp8725_set_buck_vol_v1orv2(buckn,vol,0)<0)
	return -1;

	return lp8725_set_buck_vol_v1orv2(buckn,vol,1);
	
}

int get_dvs_pin_level(void)
{

// dvs pin is low   return 0

}
int set_dvs_pin_level(uint8_t level)
{



}
/*******************************************************************************
*in sleep mode all pins val is low
*reg SLEEP_MODE=0,and pin DVS=0，this ic enter into seelp mode
********************************************************************************/
int lp8725_setinto_sleep(uint8_t sleep)
{


	uint8_t reg_val=0;
	set_dvs_pin_level(0);

	if(sleep)
		reg_val=lp8725_regs.general_reg_val|(0x01<<1);
	else
		reg_val=lp8725_regs.general_reg_val&(~0x01<<1);
	if(lp8725_write_reg(lp8725_client,GENERAL_REG_ADDR,reg_val)<0)
	{

		DBGERR("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}
	lp8725_regs.general_reg_val=reg_val;
	return 0;
}
/***************************************************************
*
*
****************************************************************/
int lp8725_buck_en(uint8_t buckn,uint8_t enble)
{


	uint8_t reg_val=0;

	if(buckn==1)
	{
		if(enble)
			reg_val=lp8725_regs.general_reg_val|0x01;
		else
			reg_val=lp8725_regs.general_reg_val&(~0x01);
	}

	 if(buckn==2)
	{
		if(enble)
			reg_val=lp8725_regs.general_reg_val|(0x01<<4);
		else
			reg_val=lp8725_regs.general_reg_val&(~0x01<<4);
	}
	 
	if(lp8725_write_reg(lp8725_client,GENERAL_REG_ADDR,reg_val)<0)
	{

		DBGERR("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}

	lp8725_regs.general_reg_val=reg_val;

	return 0;
}



int lp8725_lilo_en(uint8_t lilon,uint8_t enble)
{
	uint8_t reg_val=0;

		if(enble)
			reg_val=lp8725_regs.ldo_ctr_reg_val|(0x01<<(5+lilon-1));
		else
			reg_val=lp8725_regs.ldo_ctr_reg_val&(~(0x01<<(5+lilon-1)));
	 
	if(lp8725_write_reg(lp8725_client,LDO_CTR_REG_ADDR,reg_val)<0)
	{

		DBGERR("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}

	lp8725_regs.ldo_ctr_reg_val=reg_val;

	return 0;
}
/***************************************************************
*ldo3 is enbale if pin LdO3_EN=1 or LDO3 enable reg=1   
*
****************************************************************/
int lp8725_ldo_en(uint8_t ldo,uint8_t enble)
{


	uint8_t reg_val=0;

		if(enble)
			reg_val=lp8725_regs.ldo_ctr_reg_val|(0x01<<(ldo-1));
		else
			reg_val=lp8725_regs.ldo_ctr_reg_val&(~(0x01<<(ldo-1)));

	 
	if(lp8725_write_reg(lp8725_client,LDO_CTR_REG_ADDR,reg_val)<0)
	{
		DBGERR("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}

	lp8725_regs.ldo_ctr_reg_val=reg_val;

	return 0;
}



void lp8725_set_init(void)
{

	uint8_t reg_val=0;	
	uint8_t read_ret;
	lp8725_regs.general_reg_val=GENERAL_REG_DEFAULT;
	lp8725_regs.buck_ctr_reg_val=BUCK_CTR_REG_ADDR;
	lp8725_regs.pulldown_reg_val=PULLDOWN_REG_ADDR;
	lp8725_regs.status_reg_val=STATUS_REG_ADDR;
	/*
	开机后ldo 1、2、5 lilo2默认为关闭
	*/
	DBG("**run in %s**\n",__FUNCTION__);
	//reg_val=LDO_CTR_REG_ADDR&&(~(1<<0))&&(~(1<<0))&&(~(1<<1))&&(~(1<<4))&&(~(1<<6));
	reg_val=0x7f;
	DBG("**lp8725_client=%s**LDO_CTR_REG_ADDR=%d**reg_val=%d\n",lp8725_client,LDO_CTR_REG_ADDR,reg_val);
	if(lp8725_write_reg(lp8725_client,LDO_CTR_REG_ADDR,reg_val)<0)
	{
		DBG("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}	

	/*
	**set ldo4 VCCA 3.3V;ldo5 AVDD18 1.8v
	*/
	lp8725_set_ldo_vol(4,330);
	lp8725_set_ldo_vol(5,180);





	
#if 0
	DBG("**begin lp8725_read_reg %s %d \n",__FUNCTION__,__LINE__);
	if(lp8725_read_reg(lp8725_client,0x00,&read_ret)<0)
	{
		DBG("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}
	DBG("**lp8725_read_reg %s %d return read_ret = %d\n",__FUNCTION__,__LINE__,read_ret);
#endif

	lp8725_regs.ldo_ctr_reg_val=reg_val;
	
	DBG("**run out %s**\n",__FUNCTION__);
}

#if 0
static __init int seqgen1_init(void)
{
	uint8_t *read_val;
	//uint8_t reg_val=0x98;
	DBG("**************run in %s\n",__FUNCTION__);
	//lp8725_set_ldo_vol(2,150);
	//lp8725_set_lilo_vol(2,150);
	//lp8725_set_buck_vol(2,170);

	while(1)
	{
		DBG("\n***lp8725_lilo_en(2,0)\n");
		lp8725_lilo_en(2,0);
		mdelay(6000);
		DBG("\n***lp8725_lilo_en(2,1)\n");
		lp8725_lilo_en(2,1);
		mdelay(6000);
	}
	
#if 0
	while(1)
	{
		int i;
		for(i=180;i<=330;i=i-10)
		{
			DBG("\n\n**i=%d\n",i);
			DBG("**************run in %s\n",__FUNCTION__);
			lp8725_set_buck_vol(2,i);
			mdelay(3000);
		}		
	}
	DBG("**************run out %s\n",__FUNCTION__);
	
	
	if(lp8725_write_reg(lp8725_client,LDO2_REG_ADDR,reg_val)<0)
	{
		DBG("%s %d error\n",__FUNCTION__,__LINE__);
		return -1;
	}

	while(1)
	{
		lp8725_read_reg(lp8725_client,LDO_CTR_REG_ADDR,read_val);
		DBG("\n*****read_val = %d\n",read_val);
	}	
	DBG("**************run out %s\n",__FUNCTION__);
#endif	
	return 0;	
}
late_initcall(seqgen1_init);
#endif


static int __devinit lp8725_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int ret;

	DBG("**%s in %d line,dev adr is %x**\n",__FUNCTION__,__LINE__,client->addr);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;
	lp8725_client=client;
	lp8725_set_init();

	DBG("**run out %s**\n",__FUNCTION__);
    return 0;
}

static int lp8725_remove(struct i2c_client *client)
{
	
	return 0;
}

static struct i2c_driver lp8725_driver = {
    .driver = {
		  .owner  = THIS_MODULE,
        .name   = "lp8725",
    },
    .probe      = lp8725_probe,
    .remove     = lp8725_remove,
    .id_table   = lp8725_id,
};

static int __init lp8725_init(void)
{
	int ret;	
	ret = i2c_add_driver(&lp8725_driver);
	DBG("**lp8725_init return %d**\n",ret);
	return ret;
}
static void __exit lp8725_exit(void)
{
    	i2c_del_driver(&lp8725_driver);
}

module_init(lp8725_init);
module_exit(lp8725_exit);
MODULE_AUTHOR(" XXX  XXX@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk2818 extend gpio device");
MODULE_LICENSE("GPL");


