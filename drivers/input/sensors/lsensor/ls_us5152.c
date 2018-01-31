/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <mach/gpio.h>
#include <mach/board.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>
#include <linux/types.h>


#define DRIVER_VERSION		        "1.0"

#define PWR_MODE_DOWN_MASK     		0x80
#define PWR_MODE_OPERATE_MASK     0x7F


/*us5152 Slave Addr*/
#define LIGHT_ADDR      0x72

/*Interrupt PIN for S3C6410*/
#define IRQ_LIGHT_INT IRQ_EINT(6)

/*Register Set*/
#define REGS_CR0          	0x00
#define REGS_CR1          	0x01
#define REGS_CR2          	0x02
#define REGS_CR3          	0x03
//ALS
#define REGS_INT_LSB_TH_LO      0x04
#define REGS_INT_MSB_TH_LO      0x05
#define REGS_INT_LSB_TH_HI      0x06
#define REGS_INT_MSB_TH_HI      0x07
//ALS data
#define REGS_LBS_SENSOR         0x0C
#define REGS_MBS_SENSOR         0x0D

#define REGS_CR10          	0x10
#define REGS_CR11          	0x11
#define REGS_VERSION_ID      	0x1F
#define REGS_CHIP_ID      	0xB2

/*ShutDown_EN*/
#define CR0_OPERATION		0x0
#define CR0_SHUTDOWN_EN		0x1

#define CR0_SHUTDOWN_SHIFT   	(7)
#define CR0_SHUTDOWN_MASK    	(0x1 << CR0_SHUTDOWN_SHIFT)

/*OneShot_EN*/
#define CR0_ONESHOT_EN		0x01

#define CR0_ONESHOT_SHIFT   	(6)
#define CR0_ONESHOT_MASK    	(0x1 << CR0_ONESHOT_SHIFT)

/*Operation Mode*/
#define CR0_OPMODE_ALSANDPS	0x0
#define CR0_OPMODE_ALSONLY	0x1
#define CR0_OPMODE_IRONLY		0x2

#define CR0_OPMODE_SHIFT       	(4)
#define CR0_OPMODE_MASK        	(0x3 << CR0_OPMODE_SHIFT)

/*all int flag (PROX, INT_A, INT_P)*/
#define CR0_ALL_INT_CLEAR	0x0

#define CR0_ALL_INT_SHIFT       (1)
#define CR0_ALL_INT_MASK        (0x7 << CR0_ALL_INT_SHIFT)


/*indicator of object proximity detection*/
#define CR0_PROX_CLEAR		0x0

#define CR0_PROX_SHIFT       	(3)
#define CR0_PROX_MASK        	(0x1 << CR0_PROX_SHIFT)

/*interrupt status of proximity sensor*/
#define CR0_INTP_CLEAR		0x0

#define CR0_INTP_SHIFT       	(2)
#define CR0_INTP_MASK        	(0x1 << CR0_INTP_SHIFT)

/*interrupt status of ambient sensor*/
#define CR0_INTA_CLEAR		0x0

#define CR0_INTA_SHIFT       	(1)
#define CR0_INTA_MASK        	(0x1 << CR0_INTA_SHIFT)

/*Word mode enable*/
#define CR0_WORD_EN		0x1

#define CR0_WORD_SHIFT       	(0)
#define CR0_WORD_MASK        	(0x1 << CR0_WORD_SHIFT)


/*ALS fault queue depth for interrupt enent output*/
#define CR1_ALS_FQ_1		0x0
#define CR1_ALS_FQ_4		0x1
#define CR1_ALS_FQ_8		0x2
#define CR1_ALS_FQ_16		0x3
#define CR1_ALS_FQ_24		0x4
#define CR1_ALS_FQ_32		0x5
#define CR1_ALS_FQ_48		0x6
#define CR1_ALS_FQ_63		0x7

#define CR1_ALS_FQ_SHIFT       	(5)
#define CR1_ALS_FQ_MASK        	(0x7 << CR1_ALS_FQ_SHIFT)

/*resolution for ALS*/
#define CR1_ALS_RES_12BIT	0x0
#define CR1_ALS_RES_14BIT	0x1
#define CR1_ALS_RES_16BIT	0x2
#define CR1_ALS_RES_16BIT_2	0x3

#define CR1_ALS_RES_SHIFT      	(3)
#define CR1_ALS_RES_MASK       	(0x3 << CR1_ALS_RES_SHIFT)

/*sensing amplifier selection for ALS*/
#define CR1_ALS_GAIN_X1		0x0
#define CR1_ALS_GAIN_X2		0x1
#define CR1_ALS_GAIN_X4		0x2
#define CR1_ALS_GAIN_X8		0x3
#define CR1_ALS_GAIN_X16	0x4
#define CR1_ALS_GAIN_X32	0x5
#define CR1_ALS_GAIN_X64	0x6
#define CR1_ALS_GAIN_X128	0x7

#define CR1_ALS_GAIN_SHIFT      (0)
#define CR1_ALS_GAIN_MASK       (0x7 << CR1_ALS_GAIN_SHIFT)


/*PS fault queue depth for interrupt event output*/
#define CR2_PS_FQ_1		0x0
#define CR2_PS_FQ_4		0x1
#define CR2_PS_FQ_8		0x2
#define CR2_PS_FQ_15		0x3

#define CR2_PS_FQ_SHIFT      	(6)
#define CR2_PS_FQ_MASK       	(0x3 << CR2_PS_FQ_SHIFT)

/*interrupt type setting */
/*low active*/
#define CR2_INT_LEVEL		0x0
/*low pulse*/
#define CR2_INT_PULSE		0x1

#define CR2_INT_SHIFT      	(5)
#define CR2_INT_MASK       	(0x1 << CR2_INT_SHIFT)

/*resolution for PS*/
#define CR2_PS_RES_12		0x0
#define CR2_PS_RES_14		0x1
#define CR2_PS_RES_16		0x2
#define CR2_PS_RES_16_2		0x3

#define CR2_PS_RES_SHIFT      	(3)
#define CR2_PS_RES_MASK       	(0x3 << CR2_PS_RES_SHIFT)

/*sensing amplifier selection for PS*/
#define CR2_PS_GAIN_1		0x0
#define CR2_PS_GAIN_2		0x1
#define CR2_PS_GAIN_4		0x2
#define CR2_PS_GAIN_8		0x3
#define CR2_PS_GAIN_16		0x4
#define CR2_PS_GAIN_32		0x5
#define CR2_PS_GAIN_64		0x6
#define CR2_PS_GAIN_128		0x7

#define CR2_PS_GAIN_SHIFT      	(0)
#define CR2_PS_GAIN_MASK       	(0x7 << CR2_PS_GAIN_SHIFT)

/*wait-time slot selection*/
#define CR3_WAIT_SEL_0		0x0
#define CR3_WAIT_SEL_4		0x1
#define CR3_WAIT_SEL_8		0x2
#define CR3_WAIT_SEL_16		0x3

#define CR3_WAIT_SEL_SHIFT      (6)
#define CR3_WAIT_SEL_MASK       (0x3 << CR3_WAIT_SEL_SHIFT)

/*IR-LED drive peak current setting*/
#define CR3_LEDDR_12_5		0x0
#define CR3_LEDDR_25		0x1
#define CR3_LEDDR_50		0x2
#define CR3_LEDDR_100		0x3

#define CR3_LEDDR_SHIFT      	(4)
#define CR3_LEDDR_MASK       	(0x3 << CR3_LEDDR_SHIFT)

/*INT pin source selection*/
#define CR3_INT_SEL_BATH	0x0
#define CR3_INT_SEL_ALS		0x1
#define CR3_INT_SEL_PS		0x2
#define CR3_INT_SEL_PSAPP	0x3

#define CR3_INT_SEL_SHIFT      	(2)
#define CR3_INT_SEL_MASK       	(0x3 << CR3_INT_SEL_SHIFT)

/*software reset for register and core*/
#define CR3_SOFTRST_EN		0x1

#define CR3_SOFTRST_SHIFT      	(0)
#define CR3_SOFTRST_MASK       	(0x1 << CR3_SOFTRST_SHIFT)

/*modulation frequency of LED driver*/
#define CR10_FREQ_DIV2		0x0
#define CR10_FREQ_DIV4		0x1
#define CR10_FREQ_DIV8		0x2
#define CR10_FREQ_DIV16		0x3

#define CR10_FREQ_SHIFT      	(1)
#define CR10_FREQ_MASK       	(0x3 << CR10_FREQ_SHIFT)

/*50/60 Rejection enable*/
#define CR10_REJ_5060_DIS	0x00
#define CR10_REJ_5060_EN	0x01

#define CR10_REJ_5060_SHIFT     (0)
#define CR10_REJ_5060_MASK      (0x1 << CR10_REJ_5060_SHIFT)

#define us5152_NUM_CACHABLE_REGS 0x12


static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	//struct sensor_private_data *sensor =
	   // (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	char value = 0;
	int i = 0;

	for(i=0; i<3; i++)
		{
			if(!enable)
			{
				value = sensor_read_reg(client, REGS_CR0);
				value |= PWR_MODE_DOWN_MASK;	//ShutDown_EN=1
				result = sensor_write_reg(client, REGS_CR0, value);
				if(result)
					return result;
			}
			else
			{
				value = sensor_read_reg(client, REGS_CR0);
				value &= PWR_MODE_OPERATE_MASK ; //Operation_EN=0
				result = sensor_write_reg(client, REGS_CR0, value);
				if(result)
					return result;
			}

			if(!result)
			break;
		}

		if(i>1)
		printk("%s:set %d times",__func__,i);


	//TODO:? function to be added here

	return result;

}


static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	char value = 0;

	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;

	value = sensor_read_reg(client, REGS_CHIP_ID); //read chip ids
	printk("us5152 chip id is %x!\n", value);

	value = 0x01;//word accessing

	result = sensor_write_reg(client, REGS_CR0, value);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	return result;
}


static int us5152_value_report(struct input_dev *input, int data)
{
	unsigned char index = 0;
	if(data <= 10){
		index = 0;goto report;
	}
	else if(data <= 160){
		index = 1;goto report;
	}
	else if(data <= 225){
		index = 2;goto report;
	}
	else if(data <= 320){
		index = 3;goto report;
	}
	else if(data <= 640){
		index = 4;goto report;
	}
	else if(data <= 1280){
		index = 5;goto report;
	}
	else if(data <= 2600){
		index = 6;goto report;
	}
	else{
		index = 7;goto report;
	}

report:
	input_report_abs(input, ABS_MISC, index);
	input_sync(input);
	return index;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int value = 0;
	char index = 0;
	char buffer[2]= { 0 } ;
	int ret=0;

	if(sensor->pdata->irq_enable)
	{
		if(sensor->ops->int_status_reg >= 0)
		{
			value = sensor_read_reg(client, sensor->ops->int_status_reg);
		}

	}

	//value = sensor_read_reg(client, sensor->ops->read_reg);  //TODO:? to be changed
	if(sensor->ops->read_len< 2) //12bit
	{
		printk("us5152 data read para num error ; len = %d\n ",sensor->ops->read_len);
		return -1;
	}
	memset(buffer , 0 , 2);
	do
	{
		*buffer = sensor->ops->read_reg;
		ret=sensor_rx_data(client,buffer,sensor->ops->read_len);
		if(ret<0)
			return ret;
	}
	while(0);
	value=buffer[1];
	value =((value << 8) | buffer[0]) & 0xffff;
	index = us5152_value_report(sensor->input_dev, value);  //now is 12bit

	//printk("%s:%s result=0x%x,index=%d\n",__func__,sensor->ops->name, value,index);
	DBG("%s:%s result=%d,index=%d buffer[1]=0x%x , buffer[0]=0x%x \n",__func__,sensor->ops->name, value,index,buffer[1],buffer[0]);

	return result;
}


struct sensor_operate light_us5152_ops = {
	.name				= "ls_us5152",
	.type				= SENSOR_TYPE_LIGHT,	//sensor type and it should be correct
	.id_i2c				= LIGHT_ID_US5152,	//i2c id number
	.read_reg			= REGS_LBS_SENSOR,	//read data
	.read_len			= 2,			//data length
	.id_reg				= REGS_CHIP_ID,		//read device id from this register
	.id_data 			= 0x26,			//device id
	.precision			= 12,			//12 bits
	.ctrl_reg 			= REGS_CR0,		//enable or disable
	.int_status_reg 		= SENSOR_UNKNOW_DATA,	//intterupt status register
	.range				= {0,10},		//range
	.brightness                     = {10,4095},                          // brightness
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT ,
	.active				= sensor_active,
	.init				= sensor_init,
	.report				= sensor_report_value,
};
/****************operate according to sensor chip:end************/

//function name should not be changed
static struct sensor_operate *light_get_ops(void)
{
	return &light_us5152_ops;
}


static int __init us5152_init(void)
{
	struct sensor_operate *ops = light_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, light_get_ops);

	return result;
}

static void __exit us5152_exit(void)
{
	struct sensor_operate *ops = light_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, light_get_ops);
}

MODULE_AUTHOR("Finley Huang finley_huang@upi-semi.com");
MODULE_DESCRIPTION("us5152 ambient light sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(us5152_init);
module_exit(us5152_exit);

