/* 
 * drivers/input/key/hv2605_keypad.c
 *
 * FocalTech ft5x0x TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
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
 *
 *	note: only support mulititouch	Wenfs 2010-10-01
 */

#include <linux/i2c.h>
#include <linux/input.h>
#include "zt8031.h"
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>


#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#include <linux/wakelock.h>
#endif

#include <plat/sys_config.h>
#include <mach/irqs.h>
#define TP_ID (0x10000000)

//////////////////////////////////////////////////////
static void* __iomem gpio_addr = NULL;
static int gpio_int_hdle = 0;
static int gpio_wakeup_hdle = 0;
static int gpio_reset_hdle = 0;
static int gpio_wakeup_enable = 1;
static int gpio_reset_enable = 1;

static int screen_max_x = 0;
static int screen_max_y = 0;
static int revert_x_flag = 0;
static int revert_y_flag = 0;

/*
 * aw_get_pendown_state  : get the int_line data state, 
 * 
 * return value:
 *             return PRESS_DOWN: if down
 *             return FREE_UP: if up,
 *             return 0: do not need process, equal free up.          
 */
static int aw_get_pendown_state(void)
{
	unsigned int reg_val;
	static int state = FREE_UP;

    //get the input port state
    reg_val = readl(gpio_addr + PIOH_DATA);
	//printk("reg_val = %x\n",reg_val);
    if(!(reg_val & (1<<IRQ_NO))) 
    {
        state = PRESS_DOWN;
        //printk("pen down\n");
        return PRESS_DOWN;
    }
    //touch panel is free up
    else   
    {
        state = FREE_UP;
        return FREE_UP;
    }
}

/**
 * aw_clear_penirq - clear int pending
 *
 */
static void aw_clear_penirq(void)
{
	int reg_val;
	//clear the IRQ_EINT29 interrupt pending
	//printk("clear pend irq pending\n");
	reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);
	//writel(reg_val,gpio_addr + PIO_INT_STAT_OFFSET);
    //writel(reg_val&(1<<(IRQ_EINT21)),gpio_addr + PIO_INT_STAT_OFFSET);
    if((reg_val = (reg_val&(1<<(IRQ_NO)))))
    {
        //printk("==IRQ_EINT29=\n");              
        writel(reg_val,gpio_addr + PIO_INT_STAT_OFFSET);
    }
}

/**
 * aw_set_irq_mode - according sysconfig's subkey "ctp_int_port" to config int port.
 * 
 * return value: 
 *              0:      success;
 *              others: fail; 
 */
static int aw_set_irq_mode(void)
{
    int reg_val;
    int ret = 0;

    return ret;
    //config gpio to int mode
    printk("config gpio to int mode. \n");
    #ifndef SYSCONFIG_GPIO_ENABLE
    #else
        if(gpio_int_hdle)
        {
            gpio_release(gpio_int_hdle, 2);
        }
        gpio_int_hdle = gpio_request_ex("ctp_para", "ctp_int_port");
        if(!gpio_int_hdle)
        {
            printk("request tp_int_port failed. \n");
            ret = -1;
            goto request_tp_int_port_failed;
        }
    #endif
    
#ifdef AW_GPIO_INT_API_ENABLE

#else
        //Config IRQ_EINT25 Negative Edge Interrupt
        reg_val = readl(gpio_addr + PIO_INT_CFG3_OFFSET);
        reg_val &=(~(7<<4));
        reg_val |=(1<<4);  
        writel(reg_val,gpio_addr + PIO_INT_CFG3_OFFSET);
        
        aw_clear_penirq();
            
        //Enable IRQ_EINT25 of PIO Interrupt
        reg_val = readl(gpio_addr + PIO_INT_CTRL_OFFSET);
        reg_val |=(1<<IRQ_NO);
        writel(reg_val,gpio_addr + PIO_INT_CTRL_OFFSET);
	    //disable_irq(IRQ_EINT);
	    	
    mdelay(2);
#endif

request_tp_int_port_failed:
    return ret;  
}

/**
 * aw_set_gpio_mode - according sysconfig's subkey "ctp_io_port" to config io port.
 *
 * return value: 
 *              0:      success;
 *              others: fail; 
 */
static int aw_set_gpio_mode(void)
{
    //int reg_val;
    int ret = 0;
    //config gpio to io mode
    printk("config gpio to io mode. \n");
    #ifndef SYSCONFIG_GPIO_ENABLE
    #else
        if(gpio_int_hdle)
        {
            gpio_release(gpio_int_hdle, 2);
        }
        gpio_int_hdle = gpio_request_ex("ctp_para", "ctp_io_port");
        if(!gpio_int_hdle)
        {
            printk("request ctp_io_port failed. \n");
            ret = -1;
            goto request_tp_io_port_failed;
        }
    #endif
    return ret;

request_tp_io_port_failed:
    return ret;
}

/**
 * aw_judge_int_occur - whether interrupt occur.
 *
 * return value: 
 *              0:      int occur;
 *              others: no int occur; 
 */
static int aw_judge_int_occur(void)
{
    //int reg_val[3];
    int reg_val;
    int ret = -1;

    reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);
    if(reg_val&(1<<(IRQ_NO)))
    {
        ret = 0;
    }
    return ret; 	
}

/**
 * aw_free_platform_resource - corresponding with aw_init_platform_resource
 *
 */
static void aw_free_platform_resource(void)
{
    if(gpio_addr){
        iounmap(gpio_addr);
    }
    if(gpio_int_hdle)
    {
    	gpio_release(gpio_int_hdle, 2);
    }
    if(gpio_wakeup_hdle){
        gpio_release(gpio_wakeup_hdle, 2);
    }
    if(gpio_reset_hdle){
        gpio_release(gpio_reset_hdle, 2);
    }
    
    return;
}


/**
 * aw_init_platform_resource - initialize platform related resource
 * return value: 0 : success
 *               -EIO :  i/o err.
 *
 */
static int aw_init_platform_resource(void)
{
	int ret = 0;
	
        gpio_addr = ioremap(PIO_BASE_ADDRESS, PIO_RANGE_SIZE);
        //printk("%s, gpio_addr = 0x%x. \n", __func__, gpio_addr);
        if(!gpio_addr) {
	    ret = -EIO;
	    goto exit_ioremap_failed;	
	}
//    gpio_wakeup_enable = 1;
    gpio_wakeup_hdle = gpio_request_ex("ctp_para", "ctp_wakeup");
    if(!gpio_wakeup_hdle) {
        pr_warning("touch panel tp_wakeup request gpio fail!\n");
        //ret = EIO;
        gpio_wakeup_enable = 0;
        //goto exit_gpio_wakeup_request_failed;
    }

    gpio_reset_hdle = gpio_request_ex("ctp_para", "ctp_reset");
    if(!gpio_reset_hdle) {
        pr_warning("touch panel tp_reset request gpio fail!\n");
        //ret = EIO;
        gpio_reset_enable = 0;
        //goto exit_gpio_reset_request_failed;
        
    }
    
    printk("TP IRQ INITAL\n");
    if(aw_set_irq_mode()){
        ret = -EIO;
        goto exit_gpio_int_request_failed;
    }

    return ret;
    
exit_gpio_int_request_failed: 
exit_ioremap_failed:
aw_free_platform_resource();
    return ret;
}


/**
 * aw_fetch_sysconfig_para - get config info from sysconfig.fex file.
 * return value:  
 *                    = 0; success;
 *                    < 0; err
 */
static int aw_fetch_sysconfig_para(void)
{
    int ret = -1;
    int ctp_used = -1;
    char name[I2C_NAME_SIZE];
    script_parser_value_type_t type = SCRIPT_PARSER_VALUE_TYPE_STRING;

    printk("%s. \n", __func__);
    
    if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_used", &ctp_used, 1)){
        pr_err("ilitek_ts: script_parser_fetch err. \n");
        goto script_parser_fetch_err;
    }
    if(1 != ctp_used){
        pr_err("ilitek_ts: ctp_unused. \n");
        //ret = 1;
        return ret;
    }

    if(SCRIPT_PARSER_OK != script_parser_fetch_ex("ctp_para", "ctp_name", (int *)(&name), &type, sizeof(name)/sizeof(int))){
            pr_err("ilitek_ts: script_parser_fetch err. \n");
            goto script_parser_fetch_err;
    }
    if(strcmp(ZT_NAME, name)){
        pr_err("ilitek_ts: name %s does not match ZT_NAME. \n", name);
        //ret = 1;
        return ret;
    }

    if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_screen_max_x", &screen_max_x, 1)){
        pr_err("ilitek_ts: script_parser_fetch err. \n");
        goto script_parser_fetch_err;
    }
    pr_info("ilitek_ts: screen_max_x = %d. \n", screen_max_x);

    if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_screen_max_y", &screen_max_y, 1)){
        pr_err("ilitek_ts: script_parser_fetch err. \n");
        goto script_parser_fetch_err;
    }
    pr_info("ilitek_ts: screen_max_y = %d. \n", screen_max_y);

    if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_revert_x_flag", &revert_x_flag, 1)){
        pr_err("ilitek_ts: script_parser_fetch err. \n");
        goto script_parser_fetch_err;
    }
    pr_info("ilitek_ts: revert_x_flag = %d. \n", revert_x_flag);

    if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_revert_y_flag", &revert_y_flag, 1)){
        pr_err("ilitek_ts: script_parser_fetch err. \n");
        goto script_parser_fetch_err;
    }
    pr_info("ilitek_ts: revert_y_flag = %d. \n", revert_y_flag);

    return 0;

script_parser_fetch_err:
    pr_notice("=========script_parser_fetch_err============\n");
    return ret;
}

/**
 * aw_ts_reset - function
 *
 */
static void aw_ts_reset(void)
{
    printk("%s. \n", __func__);
    if(gpio_reset_enable){
        if(EGPIO_SUCCESS != gpio_write_one_pin_value(gpio_reset_hdle, 0, "ctp_reset")){
            printk("ilitek_ts_reset: err when operate gpio. \n");
        }
        mdelay(TS_RESET_LOW_PERIOD);
        if(EGPIO_SUCCESS != gpio_write_one_pin_value(gpio_reset_hdle, 1, "ctp_reset")){
            printk("ilitek_ts_reset: err when operate gpio. \n");
        }
        mdelay(TS_INITIAL_HIGH_PERIOD);
    }
    
}

/**
 * aw_ts_wakeup - function
 *
 */
static void aw_ts_wakeup(void)
{
    printk("%s. \n", __func__);
    if(1 == gpio_wakeup_enable){  
        if(EGPIO_SUCCESS != gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup")){
            printk("ts_resume: err when operate gpio. \n");
        }
        mdelay(TS_WAKEUP_LOW_PERIOD);
        if(EGPIO_SUCCESS != gpio_write_one_pin_value(gpio_wakeup_hdle, 1, "ctp_wakeup")){
            printk("ts_resume: err when operate gpio. \n");
        }
        mdelay(TS_WAKEUP_HIGH_PERIOD);

    }
    
    return;
}
////////////////////////////////////////////////////////////////

static struct aw_platform_ops aw_ops = {
	.get_pendown_state = aw_get_pendown_state,
	.clear_penirq	   = aw_clear_penirq,
	.set_irq_mode      = aw_set_irq_mode,
	.set_gpio_mode     = aw_set_gpio_mode,
	.judge_int_occur   = aw_judge_int_occur,
	.init_platform_resource = aw_init_platform_resource,
	.free_platform_resource = aw_free_platform_resource,
	.fetch_sysconfig_para = aw_fetch_sysconfig_para,
	.ts_reset =          aw_ts_reset,
	.ts_wakeup =         aw_ts_wakeup,
};

struct ts_event {
	int	x;
	int	y;
	int	pressure;
};

struct zt_ts_data {
	struct input_dev	   *input_dev;
	struct ts_event		   event;
 	struct delayed_work  work;
  	struct workqueue_struct *queue;
};

static struct i2c_client *this_client;
static unsigned int tp_flg = 0;
static struct zt_ts_data *zt_ts;

static int zt_i2c_rxdata(char *rxdata, int length)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 0,
			.buf	= rxdata,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
		},
	};

    //msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);
	
	return ret;
}

static int zt_i2c_txdata(char *txdata, int length)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txdata,
		},
	};

   	//msleep(1);
	ret = i2c_transfer(this_client->adapter, msg, 1);
	if (ret < 0)
		pr_err("%s i2c write error: %d\n", __func__, ret);

	return ret;
}

static int zt_set_reg(u8 para)
{
    u8 buf[3];
    int ret = -1;

    buf[0] = para;
    ret = zt_i2c_txdata(buf, 1);
    if (ret < 0) {
        pr_err("write reg failed! %#x ret: %d", buf[0], ret);
        return -1;
    }
    
    return 0;
}

static void zt_ts_release(void)
{
	struct zt_ts_data *data = i2c_get_clientdata(this_client);
	input_report_abs(data->input_dev, ABS_PRESSURE, 0);
	input_report_key(data->input_dev, BTN_TOUCH, 0);
	input_sync(data->input_dev);
}
/*
 * We have 4 complete samples.  
 * treating X and Y values separately.  Then pick the two with the
 * least variance, and average them.
 */
static unsigned int ts_filter(int *xdata,int *ydata,int *x, int *y)
{
	int i;
	int  min_x = 0xfff;
	int  min_y = 0xfff;
	int  max_x = 0x00;
	int  max_y = 0x00;
	int  sum_x = 0x00;
	int  sum_y = 0x00;

	for(i = 0; i < 4; i++)
	{
		if(xdata[i] < min_x)
		   min_x = xdata[i];
		if(ydata[i] < min_y)
		   min_y = ydata[i];
		sum_x += xdata[i];
		sum_y += ydata[i];
		   
		if(xdata[i] > max_x)
		  max_x = xdata[i];
		if(ydata[i] > max_y)
		  max_y = ydata[i];
		 
	}

       

	*x = (sum_x - min_x - max_x) >>1;
	*y = (sum_y - min_y - max_y) >>1;

	return 0;
}
static int zt_read_data(void)
{
	struct zt_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event;
	int z1,z2;
	int x[4],y[4];
	u8 buf[2] = {0};
	int ret = -1;
	
	//printk("%s. \n", __func__);
	memset(event, 0, sizeof(struct ts_event));
	zt_set_reg(READ_X);
	ret = zt_i2c_rxdata(buf, 2);
	x[0] =(buf[0]<<4) + (buf[1]>>4);
	zt_set_reg(READ_Y);
	ret = zt_i2c_rxdata(buf, 2);
	y[0] =(buf[0]<<4) + (buf[1]>>4);
	
	zt_set_reg(READ_X);
	ret = zt_i2c_rxdata(buf, 2);
	x[1] =(buf[0]<<4) + (buf[1]>>4);
	zt_set_reg(READ_Y);
	ret = zt_i2c_rxdata(buf, 2);
	y[1] =(buf[0]<<4) + (buf[1]>>4);

	zt_set_reg(READ_X);
	ret = zt_i2c_rxdata(buf, 2);
	x[2] =(buf[0]<<4) + (buf[1]>>4);
	zt_set_reg(READ_Y);
	ret = zt_i2c_rxdata(buf, 2);
	y[2] =(buf[0]<<4) + (buf[1]>>4);

	zt_set_reg(READ_X);
	ret = zt_i2c_rxdata(buf, 2);
	x[3] =(buf[0]<<4) + (buf[1]>>4);
	zt_set_reg(READ_Y);
	ret = zt_i2c_rxdata(buf, 2);
	y[3] =(buf[0]<<4) + (buf[1]>>4);
			
	zt_set_reg(READ_Z1);
	ret = zt_i2c_rxdata(buf, 2);
	z1 =(buf[0]<<4) + (buf[1]>>4);
	zt_set_reg(READ_Z2);
	ret = zt_i2c_rxdata(buf, 2);
	z2 =(buf[0]<<4) + (buf[1]>>4);
	zt_set_reg(PWRDOWN);
	event->pressure = 1;
	//printk("z1 = %d,z2= %d\n",z1,z2);
	if((z1<10)||(z2>4000))
	  ret = -1;
	else
	{
	  ts_filter(x,y,&event->x,&event->y);
	  ret = 0;
	}
	return ret;
}

static void zt_report_value(void)
{
	struct zt_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event;
        //printk("%s. \n", __func__);
        
	input_report_abs(data->input_dev, ABS_X, (event->x |TP_ID));
	input_report_abs(data->input_dev, ABS_Y, (event->y |TP_ID) );
	//printk("event->x = %x,event->y = %x\n",event->x,event->y);
	input_report_abs(data->input_dev, ABS_PRESSURE, event->pressure);
	input_report_key(data->input_dev, BTN_TOUCH, 1);
	input_sync(data->input_dev);
}	

static void zt_read_loop(struct work_struct *work)
{
	int ret = -1;
	int i;
	int reg_data[3];
	/*
	uint32_t tmp = 0; 
	tmp = readl(PIOI_DATA);
	printk("%s. tmp = 0x%x. \n", __func__, tmp);
	*/
	
	for(i = 0;i< 16;i++);
	reg_data[0] = (readl(PIOI_DATA)>>13)&0x1;
	for(i = 0;i< 16;i++);
	reg_data[1] = (readl(PIOI_DATA)>>13)&0x1;
	for(i = 0;i< 16;i++);
	reg_data[2] = (readl(PIOI_DATA)>>13)&0x1;  
	//printk("==work=\n");
	//printk("reg_data[0]  = 0x%x,  reg_data[1]  = 0x%x, reg_data[2]  = 0x%x .\n",  reg_data[0], reg_data[1], reg_data[2]);
	
	if((!reg_data[0])&&(!reg_data[1])&&(!reg_data[2]))
	{
	        //printk("press down. \n");
		ret = zt_read_data();	
	    if (ret == 0) 
		  zt_report_value();
		  tp_flg = 0;
		  queue_delayed_work(zt_ts->queue, &zt_ts->work, POINT_DELAY);
	}else
	{
	  if(!tp_flg)
	  {
	        //printk("release up. \n");
	  	zt_ts_release();
	  }
	  tp_flg = 1;
	  queue_delayed_work(zt_ts->queue, &zt_ts->work, 5*POINT_DELAY);
	}
}


static int 
zt_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

	struct input_dev *input_dev;
	int err = 0;
	
	printk("======================================zt_ts_probe=============================================\n");
	err = aw_ops.init_platform_resource();
	if(0 != err){
	    printk("%s:aw_ops.init_platform_resource err. \n", __func__);    
	}
	
	tp_flg = 0;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	printk("==kzalloc=\n");
	zt_ts = kzalloc(sizeof(*zt_ts), GFP_KERNEL);
	if (!zt_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

        printk("i2c_set_clientdata. \n");
	this_client = client;
	i2c_set_clientdata(client, zt_ts);


	INIT_DELAYED_WORK(&zt_ts->work, zt_read_loop);
	zt_ts->queue = create_singlethread_workqueue(dev_name(&client->dev));
	if (!zt_ts->queue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

        printk("input_allocate_device. \n");
	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}
	
	zt_ts->input_dev = input_dev;

	set_bit(ABS_X, input_dev->absbit);
	set_bit(ABS_Y, input_dev->absbit);
	set_bit(ABS_PRESSURE, input_dev->absbit);
	set_bit(BTN_TOUCH, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, (0xfff|TP_ID), 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, (0xfff|TP_ID), 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 0xff, 0 , 0);


	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);

	input_dev->name		= ZT_NAME;		//dev_name(&client->dev)
	printk("input_register_device. \n");
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
		"zt_ts_probe: failed to register input device: %s\n",
		dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}


        aw_ops.set_gpio_mode();
	queue_delayed_work(zt_ts->queue, &zt_ts->work, 5*POINT_DELAY);
	printk("==probe over =\n");
  
    	return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	//cancel_work_sync(&zt_ts->work);
	//destroy_workqueue(zt_ts->queue);
exit_create_singlethread:
	printk("==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(zt_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int __devexit zt_ts_remove(struct i2c_client *client)
{
	
	struct zt_ts_data *zt_tsc = i2c_get_clientdata(client);
	input_unregister_device(zt_tsc->input_dev);
	kfree(zt_ts);
	printk("==zt_ts_remove=\n");
	//cancel_work_sync(&zt_ts->work);
	//destroy_workqueue(zt_ts->queue);
	i2c_set_clientdata(client, NULL);
	aw_ops.free_platform_resource();
	return 0;
}

static const struct i2c_device_id zt_ts_id[] = {
	{ ZT_NAME, 0 },{ }
};
MODULE_DEVICE_TABLE(i2c, zt_ts_id);

static struct i2c_driver zt_ts_driver = {
	.probe		= zt_ts_probe,
	.remove		= __devexit_p(zt_ts_remove),
	.id_table	= zt_ts_id,
	.driver	= {
		.name	  =    ZT_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init zt_ts_init(void)
{
        int ret = 0;
        printk("%s. \n", __func__);
        ret = aw_ops.fetch_sysconfig_para();
        if(ret < 0){
            return -1;
        }
	return i2c_add_driver(&zt_ts_driver);
}

static void __exit zt_ts_exit(void)
{
        printk("%s. \n", __func__);
	i2c_del_driver(&zt_ts_driver);
}
module_init(zt_ts_init);
module_exit(zt_ts_exit);

MODULE_AUTHOR("<zhengdixu@allwinnertech.com>");
MODULE_DESCRIPTION("zt8031 TouchScreen driver");
MODULE_LICENSE("GPL");

