/*drivers/cir/bu92747guw_cir.c - driver for bu92747guw
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
 */
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/bcd.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/wakelock.h>
#include <linux/miscdevice.h>
#include <linux/freezer.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/rk29_smc.h>

#include "bu92747guw_cir.h"

#if 1
#define BU92747_DBG(x...) printk(x)
#else
#define BU92747_DBG(x...)
#endif

#define CIR_IIC_SPEED 	100 * 1000

#define XIN_INPUT_FREQ 48*1000 //KHz

struct bu92747_data_info {
	struct bu92747guw_platform_data *platdata;
	struct i2c_client *client;
	char state;
	int irq;
	int base_clock;
	int sys_clock;
	struct delayed_work      dwork;
};

static struct miscdevice bu92747guw_device;

int repeat_flag=-1;
int start_flag = 0;
//mutex lock between remote and irda




#ifdef CONFIG_RK_IRDA_UART
extern int bu92747_try_lock(void);
extern void bu92747_unlock(void);
#else
int bu92747_try_lock(void) {return 1;}
void bu92747_unlock(void) {return;}
#endif



static int bu92747_cir_i2c_read_regs(struct i2c_client *client, u8 reg, u8 *buf, int len)
{
	int ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, CIR_IIC_SPEED);
	return ret; 
}

static int bu92747_cir_i2c_set_regs(struct i2c_client *client, u8 reg, u8 *buf, int len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, len, CIR_IIC_SPEED);
	return ret;
}


static int bu92747_stop(struct i2c_client *client) 
{
	u8 reg_value[2];
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	//BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	printk("line %d: enter %s\n", __LINE__, __FUNCTION__);
//	disable_irq(bu92747->irq);
	//diable clk, repeat=1
	bu92747_cir_i2c_read_regs(client, REG_SETTING0, reg_value, 2);
	reg_value[0] = reg_value[0]&0xfe;
	reg_value[1] = reg_value[1]&0xf1;
	bu92747_cir_i2c_set_regs(client, REG_SETTING0, reg_value, 2);
	start_flag = 0;
	repeat_flag = -1;
	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
}
static void bu92747_dwork_handler(struct work_struct *work)
{
	struct bu92747_data_info *bu92747 = 
		(struct bu92747_data_info *)container_of(work, struct bu92747_data_info, dwork.work);
	u8 reg_value[2];
	
	BU92747_DBG("------- sss  enter %s\n", __func__);

	if  ((repeat_flag != -10) && (repeat_flag <= -1)){
		bu92747_stop(bu92747->client);
		BU92747_DBG("----------exit %s\n", __func__);
		return ;
	}

	//set repeat=0
	bu92747_cir_i2c_read_regs(bu92747->client, REG_SETTING1, reg_value, 1);
	reg_value[0] &= 0xf0;
	bu92747_cir_i2c_set_regs(bu92747->client, REG_SETTING1, reg_value, 1);
	//printk("----------exit %s  reg_value = %d\n", __func__, reg_value[1]);


	reg_value[0] = 1;
	
	bu92747_cir_i2c_set_regs(bu92747->client, REG_SEND, reg_value, 1);
//	bu92747_cir_i2c_read_regs(bu92747->client, REG_FRMLEN1, reg_value, 2);

//	printk("frame_interval = 0x%x\n", reg_value[1], reg_value[2]);
	//power down

	BU92747_DBG("----------exit %s\n", __func__);
	return;
}


static irqreturn_t bu92747_cir_irq(int irq, void *dev_id)
{
//	u8 reg_value[2];
	struct i2c_client *client = container_of(bu92747guw_device.parent, struct i2c_client, dev);
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);

	BU92747_DBG("----------enter %s  repeat_flag = %d\n", __func__, repeat_flag);


	if (start_flag == 1){
		if (repeat_flag == -10){
			if ((bu92747->state++)%10 == 0){
				schedule_delayed_work(&bu92747->dwork, msecs_to_jiffies(0));
				bu92747->state = 0;
			} 
		}else if (((--repeat_flag%16) == 0) || (repeat_flag < 0)){
			schedule_delayed_work(&bu92747->dwork, msecs_to_jiffies(0));
		}
	}
	return IRQ_HANDLED;
}



#if 0
static int bu92747_send_data(struct i2c_client *client, struct rk29_cir_struct_info *cir) 
{
	u8 inv0,inv1;
	unsigned int hlo, hhi;
	u8 reg_value[16];
	int i, nByte;
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	struct bu92747guw_platform_data *pdata = bu92747->platdata;
	int sys_clock = bu92747->sys_clock;
	unsigned long flags;

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	//if (bu92747->state == BU92747_BUSY) {
	//	printk("line %d, func: %s, dev is busy now\n", __LINE__, __func__);
	//	return -EBUSY;
	//}

	//bu92747->state = BU92747_BUSY;

	repeat_flag = cir->repeat%16;

	bu92747_cir_i2c_read_regs(client, REG_SETTING0, reg_value, 2);
	
	inv0 = cir->inv & 0x01;
	inv1 = (cir->inv>>1) & 0x01;
	reg_value[0] = reg_value[0] | (inv0<<1) | (inv1<<2);
	reg_value[1] = (reg_value[1]&0xf0) | (repeat_flag&0x0f);
	bu92747_cir_i2c_set_regs(client, REG_SETTING0, reg_value, 2);
	BU92747_DBG("inv0 = %d, inv1 = %d, repeat=%d\n", inv0, inv1, repeat_flag);
	
	//head maybe different while repeat
	if ((bu92747->head_burst_time!=cir->head_burst_time) 
		|| (bu92747->head_space_time!=cir->head_space_time)) {
		hlo = (cir->head_space_time*sys_clock)/1000;
		hhi = (cir->head_burst_time*sys_clock)/1000;
		reg_value[0] = hlo>>8;
		reg_value[1] = hlo&0xff;
		reg_value[2] = hhi>>8;
		reg_value[3] = hhi&0xff;	
		bu92747_cir_i2c_set_regs(client, REG_HLO1, reg_value, 4);
		BU92747_DBG("hlo = 0x%x, hhi = 0x%x\n", hlo, hhi);
	}

	//switch to remote control
	//bu92747_lock();
	
	//data bit length
	reg_value[0] = cir->frame_bit_len;
	bu92747_cir_i2c_set_regs(client, REG_BITLEN, reg_value, 1);
	BU92747_DBG("frame_bit_len = 0x%x\n", cir->frame_bit_len);
	
	//data
	nByte = (cir->frame_bit_len+7)/8;
	for (i=0; i<nByte; i++) {
		reg_value[i] = ((cir->frame)>>(8*i))&0xff;
		BU92747_DBG("reg_value[%d] = %d\n", i, reg_value[i]);
	}
	bu92747_cir_i2c_set_regs(client, REG_OUT0, reg_value, nByte);
	BU92747_DBG("nByte = %d\n", nByte);
	
	//clear irq, start send
	//reg_value[0] = 1;
	//reg_value[1] = 1;
	//bu92747_cir_i2c_set_regs(client, REG_IRQC, reg_value, 2);
	
	//send ok?
//	while (gpio_get_value(pdata->intr_pin)) {
//		BU92747_DBG("line %d: data register is not null\n", __LINE__);
//	}

	//switch to irda control	
	//smc0_write(3, smc0_read(3) & 0xfbff);
	//bu92747_unlock();

	//enable_irq(bu92747->irq);
	
	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);
	
	return 0;
}
#endif



static int bu92747_set_format(struct i2c_client *client, struct rk29_cir_struct_info *cir) 
{
	u8 inv0, inv1;
	unsigned int clo, chi, clo_org, chi_org;
	unsigned int d0lo, d0hi, d1lo, d1hi;
	unsigned int end;
	unsigned int hlo = cir->carry_low , hhi = cir->carry_high;	
	int repeat;
	u32 frame_interval;
	int i, nByte;
	u8 reg_value[32];
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	int sys_clock = bu92747->sys_clock;

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (!cir)
		return -1;

	//set inv0, inv1
	inv0 = cir->inv & 0x01;
	inv1 = (cir->inv>>1) & 0x01;
	bu92747_cir_i2c_read_regs(client, REG_SETTING0, reg_value, 3);
	reg_value[0] = reg_value[0] | (inv0<<1) | (inv1<<2);
	//bu92747_cir_i2c_set_regs(client, REG_SETTING0, reg_value, 1);
	BU92747_DBG("inv0 = %d, inv1 = %d\n", inv0, inv1);


	repeat_flag = cir->repeat;
	printk("repeat 11111 =%d\n", repeat_flag);
	
	if (repeat_flag == -1){
		repeat_flag = -10;
		repeat = 0;		
	}else if (repeat_flag > 16){
		repeat = 16; 
	}else{
		repeat = repeat_flag;
	}

	repeat = repeat % 16;
	reg_value[1] = ((reg_value[1]&0xf0) | (repeat&0x0f));


	//carry
	clo = XIN_INPUT_FREQ / 3000 * hlo;
	chi = XIN_INPUT_FREQ / 3000 * hhi;
	reg_value[3] = clo>>8;
	reg_value[4] = clo&0xff;
	reg_value[5] = chi>>8;
	reg_value[6] = chi&0xff;
	BU92747_DBG("clo = 0x%x, chi = 0x%x\n", clo, chi);
	//bu92747_cir_i2c_set_regs(client, REG_CLO1, reg_value, 4);

	

	//head
	hlo = (cir->head_space_time*sys_clock)/1000;
	hhi = (cir->head_burst_time*sys_clock)/1000;
	reg_value[7] = hlo>>8;
	reg_value[8] = hlo&0xff;
	reg_value[9] = hhi>>8;
	reg_value[10] = hhi&0xff;
	BU92747_DBG("hlo = 0x%x, hhi = 0x%x\n", hlo, hhi);


	//data0
	d0lo = (cir->logic_low_space_time*sys_clock)/1000;
	d0hi = (cir->logic_low_burst_time*sys_clock)/1000;
	reg_value[11] = d0lo>>8;
	reg_value[12] = d0lo&0xff;
	reg_value[13] = d0hi>>8;
	reg_value[14] = d0hi&0xff;
	BU92747_DBG("d0lo = 0x%x, d0hi = 0x%x\n", d0lo, d0hi);


	//data1
	d1lo = (cir->logic_high_space_time*sys_clock)/1000;
	d1hi = (cir->logic_high_burst_time*sys_clock)/1000;
	reg_value[15] = d1lo>>8;
	reg_value[16] = d1lo&0xff;
	reg_value[17] = d1hi>>8;
	reg_value[18] = d1hi&0xff;
	BU92747_DBG("d1lo = 0x%x, d1hi = 0x%x\n", d1lo, d1hi);

	//end
	end = (cir->stop_bit_interval*sys_clock)/1000;
	reg_value[19] = end>>8;
	reg_value[20] = end&0xff;
	//bu92747_cir_i2c_set_regs(client, REG_CLO1, reg_value, 18);

	BU92747_DBG("end = 0x%x\n", end);

	
	//data bit length
	reg_value[21] = cir->frame_bit_len;
	BU92747_DBG("frame_bit_len = 0x%x\n", cir->frame_bit_len);

	//frame interval
	frame_interval = (cir->frame_interval*sys_clock)/1000;
	reg_value[22] = frame_interval>>8;
	reg_value[23] = frame_interval&0xff;
	//bu92747_cir_i2c_set_regs(client, REG_FRMLEN1, reg_value, 2);	
	BU92747_DBG("cir->frame_interval =%d frame_interval = %d\n\n", cir->frame_interval,frame_interval);

	
	//data
	nByte = (cir->frame_bit_len+7)/8;
	for (i=0; i<nByte; i++) {
		reg_value[24+i] = ((cir->frame)>>(8*i))&0xff;
		BU92747_DBG("reg_value[%d] = %d\n", 24+i, reg_value[24+i]);
	}

	bu92747_cir_i2c_set_regs(client, REG_SETTING0, reg_value, 24+i);
	

	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
	
}


static int bu92747_start(struct i2c_client *client) 
{
	u8 reg_value[2];
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	printk("line %d: enter %s\n", __LINE__, __FUNCTION__);


	start_flag = 1;
	bu92747->state = 0;
	bu92747_cir_i2c_read_regs(client, REG_SETTING0, reg_value, 1);
	reg_value[0] = reg_value[0]|0x01;
	bu92747_cir_i2c_set_regs(client, REG_SETTING0, reg_value, 1);

	//clear irq, start send
	reg_value[0] = 1;
	reg_value[1] = 1;
	bu92747_cir_i2c_set_regs(client, REG_IRQC, reg_value, 2);

	
	printk("line %d: exit %s\n", __LINE__, __FUNCTION__);
	return 0;
}


static void bu92747_printk_cir(struct rk29_cir_struct_info *cir)
{
	BU92747_DBG("\ncir struct:\n");
	BU92747_DBG("carry_high                  = %d\n", cir->carry_high);
	BU92747_DBG("carry_low       = %d\n", cir->carry_low);
	BU92747_DBG("repeat            = %d\n", cir->repeat);
	BU92747_DBG("inv                    = %d\n", cir->inv);
	BU92747_DBG("frame_bit_len          = %d\n", cir->frame_bit_len);
	BU92747_DBG("stop_bit_interval      = %d\n", cir->stop_bit_interval);
	BU92747_DBG("frame                  = %lld\n", cir->frame);
	BU92747_DBG("frame_interval         = %d\n", cir->frame_interval);
	BU92747_DBG("head_burst_time        = %d\n", cir->head_burst_time);
	BU92747_DBG("head_space_time        = %d\n", cir->head_space_time);
	BU92747_DBG("logic_high_burst_time  = %d\n", cir->logic_high_burst_time);
	BU92747_DBG("logic_high_space_time  = %d\n", cir->logic_high_space_time);
	BU92747_DBG("logic_low_burst_time   = %d\n", cir->logic_low_burst_time);
	BU92747_DBG("logic_low_space_time   = %d\n", cir->logic_low_space_time);
	BU92747_DBG("\n");
}


static int bu92747_set_duration(struct i2c_client *client, struct rk29_cir_struct_info *cir) 
{
	u32 frame_interval;

	u8 reg_value[20];
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	int sys_clock = bu92747->sys_clock;

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (!cir)
		return -1;

	BU92747_DBG("sys_clock = %d,  frame_interval = 0x%x\n", sys_clock,cir->frame_interval);
	//frame interval
	frame_interval = (cir->frame_interval*sys_clock)/1000;
	
	reg_value[0] = frame_interval>>8;
	reg_value[1] = frame_interval&0xff;
	bu92747_cir_i2c_set_regs(client, REG_FRMLEN1, reg_value, 2);

	BU92747_DBG("frame_interval = 0x%x\n", frame_interval);

	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
	
}

static int bu92747_set_data(struct i2c_client *client, struct rk29_cir_struct_info *cir) 
{

	u8 reg_value[16];
	int i, nByte;
	


	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	
	//data bit length
	reg_value[0] = cir->frame_bit_len;
	bu92747_cir_i2c_set_regs(client, REG_BITLEN, reg_value, 1);
	BU92747_DBG("frame_bit_len = 0x%x\n", cir->frame_bit_len);
	
	//data
	nByte = (cir->frame_bit_len+7)/8;
	for (i=0; i<nByte; i++) {
		reg_value[i] = ((cir->frame)>>(8*i))&0xff;
		BU92747_DBG("reg_value[%d] = %d\n", i, reg_value[i]);
	}
	bu92747_cir_i2c_set_regs(client, REG_OUT0, reg_value, nByte);
	BU92747_DBG("nByte = %d\n", nByte);
	
	
	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);
	
	return 0;
}

static int bu92747_set_pulse(struct i2c_client *client, struct rk29_cir_struct_info *cir) 
{
	u8 inv0, inv1;


	unsigned int d0lo, d0hi, d1lo, d1hi;


	u8 reg_value[8] = {0};
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	int sys_clock = bu92747->sys_clock;

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (!cir)
		return -1;

	//set inv0, inv1
	inv0 = cir->inv & 0x01;
	inv1 = (cir->inv>>1) & 0x01;
	bu92747_cir_i2c_read_regs(client, REG_SETTING0, reg_value, 1);
	reg_value[0] = reg_value[0] | (inv0<<1) | (inv1<<2);
	bu92747_cir_i2c_set_regs(client, REG_SETTING0, reg_value, 1);
	BU92747_DBG("inv0 = %d, inv1 = %d\n", inv0, inv1);



	//data0
	d0lo = (cir->logic_low_space_time*sys_clock)/1000;
	d0hi = (cir->logic_low_burst_time*sys_clock)/1000;
	reg_value[0] = d0lo>>8;
	reg_value[1] = d0lo&0xff;
	reg_value[2] = d0hi>>8;
	reg_value[3] = d0hi&0xff;
	BU92747_DBG("d0lo = 0x%x, d0hi = 0x%x\n", d0lo, d0hi);

	//data1
	d1lo = (cir->logic_high_space_time*sys_clock)/1000;
	d1hi = (cir->logic_high_burst_time*sys_clock)/1000;
	reg_value[4] = d1lo>>8;
	reg_value[5] = d1lo&0xff;
	reg_value[6] = d1hi>>8;
	reg_value[7] = d1hi&0xff;
	BU92747_DBG("d1lo = 0x%x, d1hi = 0x%x\n", d1lo, d1hi);
	bu92747_cir_i2c_set_regs(client, REG_D0LO1, reg_value, 8);

	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
	
}



static int bu92747_set_parameter(struct i2c_client *client, struct rk29_cir_struct_info *cir) 
{
	unsigned int hlo, hhi;
	unsigned int end;

	u8 reg_value[4];
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	int sys_clock = bu92747->sys_clock;

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (!cir)
		return -1;

	//head
	hlo = (cir->head_space_time*sys_clock)/1000;
	hhi = (cir->head_burst_time*sys_clock)/1000;
	reg_value[0] = hlo>>8;
	reg_value[1] = hlo&0xff;
	reg_value[2] = hhi>>8;
	reg_value[3] = hhi&0xff;
	BU92747_DBG("hlo = 0x%x, hhi = 0x%x\n", hlo, hhi);
	bu92747_cir_i2c_set_regs(client, REG_HLO1, reg_value, 4);

	//end
	end = (cir->stop_bit_interval*sys_clock)/1000;
	reg_value[0] = end>>8;
	reg_value[1] = end&0xff;
	bu92747_cir_i2c_set_regs(client, REG_ENDLEN1, reg_value, 2);

	BU92747_DBG("end = 0x%x\n", end);

	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
	
}


static int bu92747_set_repeat(struct i2c_client *client, struct rk29_cir_struct_info *cir) 
{

	u8 reg_value[2];
	int repeat;

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);


    repeat_flag = cir->repeat;
	printk("repeat 11111 =%d\n", repeat_flag);
	
	if (repeat_flag == -1){
		repeat_flag = -10;
		repeat = 0;		
	}else if (repeat_flag > 16){
		repeat = 16; 
	}else{
		repeat = repeat_flag;
	}


	repeat = repeat % 16;
	
	bu92747_cir_i2c_read_regs(client, REG_SETTING1, reg_value, 1);
	
	reg_value[0] = (reg_value[0]&0xf0) | (repeat&0x0f);
	bu92747_cir_i2c_set_regs(client, REG_SETTING1, reg_value, 1);
	printk("repeat  2222  =%d  reg_value = %d\n", repeat, reg_value[0]);
	 
	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);
	
	return 0;
}

static int bu92747_set_carrier(struct i2c_client *client, struct rk29_cir_struct_info *cir) 
{
	u8 reg_value[4];
	u16 clo = 0, chi = 0;  
	unsigned int hlo = cir->carry_low , hhi = cir->carry_high;	

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (!cir)
		return -1;

	//carry

	clo = XIN_INPUT_FREQ / 3000 * hlo;
	chi = XIN_INPUT_FREQ / 3000 * hhi;
	reg_value[0] = clo>>8;
	reg_value[1] = clo&0xff;
	reg_value[2] = chi>>8;
	reg_value[3] = chi&0xff;
	BU92747_DBG("clo = 0x%x, chi = 0x%x\n", clo, chi);
	bu92747_cir_i2c_set_regs(client, REG_CLO1, reg_value, 4);

	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
	
}


static int bu92747_cir_init_device(struct i2c_client *client, struct bu92747_data_info *bu92747)	
{
	u8 reg_value[3];
	
	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	
	//transmission buff null intertupt, base clock div, irq enable, clock power up
	//reg_value[0] = /*REG0_OPM | */REG0_DIVS | REG0_PWR | REG0_IRQE;
	reg_value[0] = /*REG0_OPM | */REG0_DIVS | REG0_IRQE;
	reg_value[1] = REG1_FRME | REG1_RPT;  //enable frame interval, repeat = 1
	reg_value[2] = 80;   //base clock = 100KHz
	BU92747_DBG("line %d: reg0=0x%x, reg1=0x%x, reg2=0x%x\n", __LINE__, reg_value[0], reg_value[1], reg_value[2]);
	bu92747_cir_i2c_set_regs(client, REG_SETTING0, reg_value, 3);
	bu92747_cir_i2c_read_regs(client, REG_SETTING0, reg_value, 3);
	BU92747_DBG("line %d: reg0=0x%x, reg1=0x%x, reg2=0x%x\n", __LINE__, reg_value[0], reg_value[1], reg_value[2]);

	bu92747->base_clock = 100; //KHz
	bu92747->sys_clock = bu92747->base_clock;

	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
}

static int bu92747_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	char msg[CIR_FRAME_SIZE];
	int ret;
	struct i2c_client *client = container_of(bu92747guw_device.parent, struct i2c_client, dev);

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);


	switch (cmd) {
	
	case BU92747_IOCTL_START:
		ret = bu92747_start(client);
		if (ret < 0)
			return ret;
		break;
		
	case BU92747_IOCTL_STOP:
		ret = bu92747_stop(client);
		if (ret < 0)
			return ret;
		break;
		
	case BU92747_IOCTL_PULSE:
		if (copy_from_user(&msg, argp, sizeof(msg)))
			return -EFAULT;
			
		//bu92747_printk_cir((struct rk29_cir_struct_info *)msg);
		ret = bu92747_set_pulse(client, (struct rk29_cir_struct_info *)msg);
		if (ret < 0)
			return ret;
		break;
    
	case BU92747_IOCTL_DATA:
		if (copy_from_user(&msg, argp, sizeof(msg)))
			return -EFAULT;
		ret = bu92747_set_data(client, (struct rk29_cir_struct_info *)msg);
		if (ret < 0)
			return ret;
		break;

	case BU92747_IOCTL_CARRIER:
		if (copy_from_user(&msg, argp, sizeof(msg)))
			return -EFAULT;
		ret = bu92747_set_carrier(client, (struct rk29_cir_struct_info *)msg);
		if (ret < 0)
			return ret;
		break;	
	case BU92747_IOCTL_REPEAT:
		if (copy_from_user(&msg, argp, sizeof(msg)))
			return -EFAULT;
		ret = bu92747_set_repeat(client, (struct rk29_cir_struct_info *)msg);
		if (ret < 0)
			return ret;	
		break;
		
	case BU92747_IOCTL_DURATION:
		if (copy_from_user(&msg, argp, sizeof(msg)))
			return -EFAULT;
		ret = bu92747_set_duration(client, (struct rk29_cir_struct_info *)msg);
		if (ret < 0)
			return ret;	
		break;	
	case BU92747_IOCTL_PARAMETER:
		if (copy_from_user(&msg, argp, sizeof(msg)))
			return -EFAULT;
			
		ret = bu92747_set_parameter(client, (struct rk29_cir_struct_info *)msg);
		if (ret < 0)
			return ret;		
		break;	
	case BU92747_IOCTL_FORMATE:
			if (copy_from_user(&msg, argp, sizeof(msg)))
				return -EFAULT;
				
			ret = bu92747_set_format(client, (struct rk29_cir_struct_info *)msg);
			if (ret < 0)
				return ret; 	
			break;	

		
	default:
		return -ENOTTY;
	}
	
	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
}

static int bu92747_open(struct inode *inode, struct file *file)
{
	struct i2c_client *client = container_of(bu92747guw_device.parent, struct i2c_client, dev);
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	struct bu92747guw_platform_data *pdata = bu92747->platdata;
	int ret = 0;
	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	printk("bu92747_open\n");


	ret = bu92747_try_lock();
	if (ret == 0){
		printk("cannot get lock. Please close irda!\n");
		return -2;
	}
	
	bu92747->state = 0;
	start_flag = 0;
	repeat_flag = -1;
//	if (BU92747_OPEN == bu92747->state) 
//		return -EBUSY;
//	bu92747->state = BU92747_OPEN;
	
	//power on
	if (pdata && pdata->cir_pwr_ctl) {
		pdata->cir_pwr_ctl(1);
	}
		
	//switch to remote control, mcr, ec_en=1,rc_mode=1
	smc0_write(REG_MCR_ADDR, smc0_read(REG_MCR_ADDR)|(3<<10));
	//set irda pwdownpin = 0
	smc0_write(REG_TRCR_ADDR, smc0_read(REG_TRCR_ADDR)&0xffbf);
	BU92747_DBG("irda power down pin = %d\n", gpio_get_value(RK29_PIN5_PA7));

	//register init
	bu92747_cir_init_device(client, bu92747);
	start_flag = 0;
	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
}

static int bu92747_release(struct inode *inode, struct file *file)
{
	struct i2c_client *client = container_of(bu92747guw_device.parent, struct i2c_client, dev);
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	struct bu92747guw_platform_data *pdata = bu92747->platdata;

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	smc0_write(REG_TRCR_ADDR, smc0_read(REG_TRCR_ADDR)|0x0040);
	start_flag = -1;

	//power down
	if (pdata && pdata->cir_pwr_ctl) {
		pdata->cir_pwr_ctl(0);
	}
	
//	bu92747->state = BU92747_CLOSE;
	bu92747_unlock();

	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);
	
	return 0;
}

#if  CONFIG_PM
static int bu92747_suspend(struct i2c_client *i, pm_message_t mesg)
{
	struct i2c_client *client = container_of(bu92747guw_device.parent, struct i2c_client, dev);
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	struct bu92747guw_platform_data *pdata = bu92747->platdata;

	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (start_flag == 0){
		BU92747_DBG("realy suspend!\n");
		disable_irq(bu92747->irq);
		smc0_write(REG_TRCR_ADDR, smc0_read(REG_TRCR_ADDR)|0x0040);
		start_flag = 0;
		repeat_flag = -1;
		
		if (pdata && pdata->cir_pwr_ctl) {
			pdata->cir_pwr_ctl(0);
		}
	}
	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);

	return 0;
}

static int bu92747_resume(struct i2c_client *i)
{
	struct i2c_client *client = container_of(bu92747guw_device.parent, struct i2c_client, dev);
	struct bu92747_data_info *bu92747 = (struct bu92747_data_info *)i2c_get_clientdata(client);
	struct bu92747guw_platform_data *pdata = bu92747->platdata;
	
	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (start_flag == 0){
		BU92747_DBG("realy resume!\n");
		enable_irq(bu92747->irq);
		bu92747->state = 0;

		//power on
		if (pdata && pdata->cir_pwr_ctl) {
			pdata->cir_pwr_ctl(1);
		}
			
		//switch to remote control, mcr, ec_en=1,rc_mode=1
		smc0_write(REG_MCR_ADDR, smc0_read(REG_MCR_ADDR)|(3<<10));
		//set irda pwdownpin = 0
		smc0_write(REG_TRCR_ADDR, smc0_read(REG_TRCR_ADDR)&0xffbf);
		BU92747_DBG("irda power down pin = %d\n", gpio_get_value(RK29_PIN5_PA7));

		//register init
		bu92747_cir_init_device(client, bu92747);
	}
	
	printk("line %d: exit %s\n", __LINE__, __FUNCTION__);


	return 0;
	
}
#else
#define bu92747_suspend NULL
#define bu92747_resume NULL
#endif

static struct file_operations bu92747_fops = {
	.owner = THIS_MODULE,
	.open = bu92747_open,
	.release = bu92747_release,
	.ioctl = bu92747_ioctl,
};

static struct miscdevice bu92747guw_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rk29_cir",
	.fops = &bu92747_fops,
};

static int __devinit bu92747_cir_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct bu92747_data_info *bu92747;
	struct bu92747guw_platform_data *pdata;
	int err;
	
	BU92747_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;
	
	bu92747 = kzalloc(sizeof(struct bu92747_data_info), GFP_KERNEL);
	if (!bu92747) {
		printk("line %d: bu92747 alloc data failed.\n", __LINE__);
		err = -ENOMEM;
		goto MEM_ERR;
	}
	//ioinit
	pdata = client->dev.platform_data;
	if (pdata && pdata->iomux_init) {
		pdata->iomux_init();
	}
	
	bu92747->platdata = pdata;
	bu92747->client = client;
	i2c_set_clientdata(client, bu92747);
	bu92747->state = 0;
	start_flag = -1;

	//register device
	bu92747guw_device.parent = &client->dev;
	err = misc_register(&bu92747guw_device);
	if (err) {
		printk("line %d: bu92747 misc_register failed.\n", __LINE__);
		goto REGISTER_ERR;
	}

	//request irq
	if (pdata && gpio_is_valid(pdata->intr_pin)) {
		printk("-------request irq\n");
		err = gpio_request(pdata->intr_pin, "rk29 cir irq");
		if (err) {
			printk("line %d: bu92747 request gpio failed.\n", __LINE__);
			goto GPIO_ERR;
		}
		gpio_direction_input(pdata->intr_pin);
		gpio_request(RK29_PIN5_PA7, NULL);
		if (err) {
			printk("line %d: bu92747 request gpio failed.\n", __LINE__);
		}
		gpio_direction_input(RK29_PIN5_PA7);
		bu92747->irq = gpio_to_irq(pdata->intr_pin);
		err = request_irq(bu92747->irq, bu92747_cir_irq, IRQF_TRIGGER_FALLING, client->dev.driver->name, bu92747);
		if (err) {
			BU92747_DBG("line %d: bu92747 request gpio failed.\n", __LINE__);
			goto IRQ_ERR;
		}
		//disable_irq(bu92747->irq);
	}
	
	//INIT_DELAYED_WORK(&bu92747->dwork, bu92747_dwork_handler);
	INIT_DELAYED_WORK(&bu92747->dwork, bu92747_dwork_handler);
	

#if 0
	//bu92747_start(client);
	//while (1) {
		//bu92747_send_data_test(client);
		//BU92747_DBG("line %d: test %s\n", __LINE__, __FUNCTION__);
		//mdelay(500);
	//}
#endif

	BU92747_DBG("line %d: exit %s\n", __LINE__, __FUNCTION__);
	return 0;

IRQ_ERR:
	gpio_free(pdata->intr_pin);
GPIO_ERR:
    misc_deregister(&bu92747guw_device);
REGISTER_ERR:
	if (pdata && pdata->iomux_deinit)
		pdata->iomux_deinit();
	kfree(bu92747);
MEM_ERR:
	return err;
}

static int __devexit bu92747_cir_remove(struct i2c_client *client)
{
	
	struct bu92747_data_info *bu92747 = i2c_get_clientdata(client);
	struct bu92747guw_platform_data *pdata = bu92747->platdata;

	printk(" cir_remove \n");
	start_flag = -1;
	free_irq(bu92747->irq, bu92747);
	gpio_free(pdata->intr_pin);
    misc_deregister(&bu92747guw_device);
	if (pdata && pdata->iomux_deinit)
		pdata->iomux_deinit();
	kfree(bu92747);
	return 0;
}

static const struct i2c_device_id bu92747_cir_id[] = {
	{ "bu92747_cir", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bu92747_cir_id);

static struct i2c_driver bu92747_cir_driver = {
	.driver		= {
		.name	= "bu92747_cir",
		.owner	= THIS_MODULE,
	},
	.probe		= bu92747_cir_probe,
	.remove		= __devexit_p(bu92747_cir_remove),
	.id_table	= bu92747_cir_id,
#ifdef CONFIG_PM
	.suspend = bu92747_suspend,
	.resume = bu92747_resume,
#endif
};

static int __init bu92747_cir_init(void)
{
	return i2c_add_driver(&bu92747_cir_driver);
}

static void __exit bu92747_cir_exit(void)
{
	i2c_del_driver(&bu92747_cir_driver);
}

MODULE_AUTHOR("zyw zyw@rock-chips.com");
MODULE_DESCRIPTION("bu92747 cir driver");
MODULE_LICENSE("GPL");

module_init(bu92747_cir_init);
module_exit(bu92747_cir_exit);

