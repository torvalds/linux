/*
 * drivers/input/touchscreen/gt801_ts.c
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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/input/mt.h>
#include <asm/mach/time.h>
#include "ct360_calib.h"
#include "ct360_ch.h"

#define CT360_DEBUG			0
#define MYCT360_DEBUG                 0

#if CT360_DEBUG
	#define ct360printk(msg...)	printk(msg);
#else
	#define ct360printk(msg...)
#endif

#if 0
	#define  yj_printk(msg...)  printk(msg);
#else
	#define  yj_printk(msg...)
#endif

#if 0
	#define  boot_printk(msg...)  printk(msg);
#else
	#define  boot_printk(msg...)
#endif


#if MYCT360_DEBUG
	#define myct360printk(msg...)	printk(msg);
#else
	#define myct360printk(msg...)
#endif

static int touch_flag_up=0;
static int touch_flag_down = 0;
static int flag=0;
static int last_num_point=1;

static int last_x[2]={0,0};
static int last_y[2]={0,0};

#define ct360_TS_NAME "ct360_ts"
#define TOUCH_NUMBER 5
#define TOUCH_REG_NUM 4 
#define IOMUX_NAME_SIZE 48

enum regadd {
	ptxh = 0, ptxl = 1, ptyh = 2, ptyl = 3, ptpressure = 4,
};
enum touchstate {
	TOUCH_UP = 0, TOUCH_DOWN = 1,
};



const unsigned char GT801_RegData[]={	
	0x0F,0x02,0x04,0x28,0x02,0x14,0x14,0x10,0x28,0xFA,0x03,0x20,0x05,0x00,0x01,
	0x23,0x45,0x67,0x89,0xAB,0xCD,0xE1,0x00,0x00,0x35,0x2E,0x4D,0xC1,0x20,0x05,
	0x00,0x80,0x50,0x3C,0x1E,0xB4,0x00,0x33,0x2C,0x01,0xEC,0x00,0x32,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
};

/*tochscreen private data*/
static int touch_state[TOUCH_NUMBER] = {TOUCH_UP,TOUCH_UP};
struct ct360_ts_data  *ct360;
#ifdef CONFIG_HAS_EARLYSUSPEND
static void ct360_ts_early_suspend(struct early_suspend *h);
static void ct360_ts_late_resume(struct early_suspend *h);
#endif


static int ct360_read_regs(struct i2c_client *client, u8 buf[], unsigned len)
{
	int ret;
	ret = i2c_master_normal_recv(client, buf, len, 400*1000);
	if(ret < 0)
		printk("ct360_ts_work_func:i2c_transfer fail =%d\n",ret);
	return ret;
}
/* set the ct360 registe,used i2c bus*/
static int ct360_write_regs(struct i2c_client *client, u8 const buf[], unsigned short len)
{
	int ret;
	ret = i2c_master_normal_send(client, buf, len, 100*1000);
 	if (ret < 0) {
	  printk("ct360_ts_work_func:i2c_transfer fail =%d\n",ret);
    }
	return ret;
}

extern char Binary_Data[16384]; 

char CT360_CTP_BootLoader(struct ct360_ts_data *ts)
{
	char value = 0;
	char I2C_Buf[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	char i = 0;
	unsigned int Flash_Address = 0;
	char CheckSum;

	//Step 00 : initBootLoader
	ts->client->addr = 0x7F;
	I2C_Buf[0] = 0x00;
	I2C_Buf[1] = 0xA5;
	ct360_write_regs(ts->client,I2C_Buf,2); // Write a “A5H” to CT360
	mdelay(15);

	// Read CT360 status
	i2c_master_normal_send(ts->client,I2C_Buf,1,100*1000);	
	mdelay(1);
	i2c_master_normal_recv(ts->client,&value,1,100*1000);

	boot_printk("%s......0...\n",__FUNCTION__);
	// if return “AAH” then going next step
	if (value != 0xAA)
		return 0;
	boot_printk("%s......1...\n",__FUNCTION__);
	//Step 0 : force CT360 generate check sum for host to compare data.
	//VTL_Address = 0x00A4
	//Prepare get check sum from CT360
	I2C_Buf[0] = 0x00;
	I2C_Buf[1] = 0x99; //Generate check sum command
	I2C_Buf[2] = (char)(0x00A4 >> 8); //define a flash address for CT360 to generate check sum
	I2C_Buf[3] = (char)(0x00A4 & 0xFF); //
	I2C_Buf[4] = 0x08; //Define a data length for CT360 to generate check sum
	ct360_write_regs(ts->client,I2C_Buf, 5); //Write Genertate check sum command to CT360
	mdelay(2); //Delay 1mS
	
	I2C_Buf[0] = 0x00;
	ct360_write_regs(ts->client,I2C_Buf,1); 
	mdelay(1);
	i2c_master_normal_recv(ts->client,I2C_Buf, 13,100*1000); // Read check sum and flash data from CT360

	//Compare host check sum with CT360 check sum(I2C_Buf[5]  I2C_Buf[9] )
	if ((I2C_Buf[5] != 'V') || (I2C_Buf[9] != 'T'))
		return 0;	
	boot_printk("%s......2..\n",__FUNCTION__);
	//Step 1 : initBootLoader
	I2C_Buf[0] = 0x00;
	I2C_Buf[1] = 0xA5;
	ct360_write_regs(ts->client,I2C_Buf,2); // Write a “A5H” to CT360
	mdelay(15);

	I2C_Buf[0] = 0x00;
	ct360_write_regs(ts->client,I2C_Buf,1); // Write a “A5H” to CT360
	mdelay(1);
	i2c_master_normal_recv(ts->client,&value, 1,100*1000);


	// if return “AAH” then going next step
	if (value != 0xAA)
	return 0;

	boot_printk("%s......3...\n",__FUNCTION__);
	//Step 2 : erase flash section 0~7
	for(i = 0; i<8; i++)
	{
		I2C_Buf[0] = 0x00;
		I2C_Buf [1] = 0x33; //Erase command
		I2C_Buf [2] = 0x00 + (i * 8); //Flash section address
		ct360_write_regs(ts->client,I2C_Buf, 3); //Write “33H” and “Flash section” to CT360
		mdelay(80); //Delay 75mS

		I2C_Buf[0] = 0x00;
		ct360_write_regs(ts->client,I2C_Buf,1); // Write a “A5H” to CT360
		mdelay(1);
		i2c_master_normal_recv(ts->client,&value, 1,100*1000);

		// if CT360 return “AAH” then going next step
		if (value != 0xAA)
			return 0; //CT360 out of controlled
	}
	boot_printk("%s......4...\n",__FUNCTION__);
	
	for (Flash_Address=0; Flash_Address < 0x3fff; Flash_Address+=8)
	{
		//Step 3 : write binary data to CT360
		I2C_Buf[0] = 0x00;
		I2C_Buf[1] = 0x55; //Flash write command
		I2C_Buf[2] = (char)(Flash_Address >> 8); //Flash address [15:8]
		I2C_Buf[3] = (char)(Flash_Address & 0xFF); //Flash address [7:0]
		I2C_Buf[4] = 0x08; //How many prepare to write to CT360
		I2C_Buf[6] = Binary_Data[Flash_Address + 0]; //Binary data 1
		I2C_Buf[7] = Binary_Data[Flash_Address + 1]; //Binary data 2
		I2C_Buf[8] = Binary_Data[Flash_Address + 2]; //Binary data 3
		I2C_Buf[9] = Binary_Data[Flash_Address + 3]; //Binary data 4
		I2C_Buf[10] = Binary_Data[Flash_Address + 4]; //Binary data 5
		I2C_Buf[11] = Binary_Data[Flash_Address + 5]; //Binary data 6
		I2C_Buf[12] = Binary_Data[Flash_Address + 6]; //Binary data 7
		I2C_Buf[13] = Binary_Data[Flash_Address + 7]; //Binary data 8
		// Calculate a check sum by Host controller.
		// Checksum = / (FLASH_ADRH+FLASH_ADRL+LENGTH+
		// Binary_Data1+Binary_Data2+Binary_Data3+Binary_Data4+
		// Binary_Data5+Binary_Data6+Binary_Data7+Binary_Data8) + 1
		CheckSum = ~(I2C_Buf[2] + I2C_Buf[3] + I2C_Buf[4] + I2C_Buf[6] + I2C_Buf[7] +
		I2C_Buf[8] + I2C_Buf[9] + I2C_Buf[10] + I2C_Buf[11] + I2C_Buf[12] +
		I2C_Buf[13]) + 1;
		I2C_Buf[5] = CheckSum; //Load check sum
		ct360_write_regs(ts->client,I2C_Buf, 14); //Host write I2C_Buf[0…12] to CT360.
		mdelay(2); //Delay 2mS

		I2C_Buf[0] = 0x00;
		ct360_write_regs(ts->client,I2C_Buf,1); // Write a “A5H” to CT360
		mdelay(2);
		i2c_master_normal_recv(ts->client,&value, 1,100*1000);

// if return “AAH” then going next step
		if (value != 0xAA)
				return 0;	
	
	
		//Step 4 : force CT360 generate check sum for host to compare data.
		//Prepare get check sum from CT360
		I2C_Buf[0] = 0x00;
		I2C_Buf[1] = 0x99; //Generate check sum command
		I2C_Buf[2] = (char)(Flash_Address >> 8); //define a flash address for CT360 to generate check sum
		I2C_Buf[3] = (char)(Flash_Address & 0xFF); //
		I2C_Buf[4] = 0x08; //Define a data length for CT360 to generate check sum
		ct360_write_regs(ts->client,I2C_Buf, 5); //Write Genertate check sum command to CT360
		mdelay(5); //Delay 1mS
		I2C_Buf[0] = 0x00;
		ct360_write_regs(ts->client,I2C_Buf,1); 
		mdelay(1);
		i2c_master_normal_recv(ts->client,I2C_Buf, 13,100*1000); // Read check sum and flash dat	//Compare host check sum with CT360 check sum(I2C_Buf[4])

		if (I2C_Buf[4] != CheckSum)
			return 0;
					
	}
	
	boot_printk("%s.....7..Flash_Address=%4x.\n",__FUNCTION__,Flash_Address);
	ts->client->addr = 0x01;
	boot_printk("%s.....7...%d\n",__FUNCTION__,ts->client->addr);
				return 1; // Boot loader function is completed.
		
	} 

/*read the ct360 register ,used i2c bus*/

static int ct360_init_panel(struct ct360_ts_data *ts)
{
    return 0;
}

static void report_value(int x,int y,struct ct360_ts_data *ts)
{
	myct360printk("%s(%d,%d)\n", __FUNCTION__,x, y);
	//if((x>ts->x_max)||(y>ts->y_max))
	//	return;
	myct360printk("x=%d,y=%d,swap=%d\n",x,y,ts->swap_xy);
	if (ts->swap_xy){
		swap(x, y);
	}
	if((x==0)||(y==0))
		return;
	input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1); //Finger Size
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 1); //Touch Size
	input_mt_sync(ts->input_dev);
	
}

static void ct360_ts_work_func(struct work_struct *work)
{
	//struct timespec now;
    //now = current_kernel_time();//(&now);
	//printk("now %lu:%lu\n",now.tv_sec,now.tv_nsec);
	
	unsigned short x = 0;
	unsigned short y = 0;
	int i,ret,syn_flag = 0;
	char toatl_num = 0;
	int bufLen = 0;
	unsigned char buf[TOUCH_REG_NUM*TOUCH_NUMBER+1] = {0};
	int point_status;
	int point_id;
	int touch_state_index=0;
	int pendown = 0;
	struct ct360_ts_data *ts = container_of(work, struct ct360_ts_data, work);
	//printk("before read the gpio_get_value(ts->client->irq) is %d\n",gpio_get_value(ts->client->irq));
	
	ret= ct360_read_regs(ts->client, buf, 20);//only one data  represent the current touch num
	if (ret < 0) {
	  	printk("%s:i2c_transfer fail =%d\n", __FUNCTION__, toatl_num);
   	  	//enable_irq(ts->irq);
		return;
	}

    for (i = 0; i < 20; i += TOUCH_REG_NUM)
    {
		point_status = buf[i + 3] & 0x0F;
		//if (point_status != 0)
		{
			point_id = buf[i+3] >> 4;
			if((point_status == 1) || (point_status == 2)) {
				x = (((s16)buf[i] << 4)|((s16)buf[i+2] >> 4));
				y = (((s16)buf[i+1] << 4)|((s16)buf[i+2] & 0x0f));
				printk("x=%d,y=%d\n",x,y);
				printk("buf: 0x%02x, 0x%02x, 0x%02x, 0x%02x\n", buf[i], buf[i+1], buf[i+2], buf[i+3]);
				input_mt_slot(ts->input_dev, point_id);
				input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
				input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, point_id);
				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 100); //Finger Size
				input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
				input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 100); //Touch Size
			
				syn_flag = 1;
			//	touch_state[touch_state_index] = TOUCH_DOWN;
				//printk("TOUCH_DOWN\n");
			//	last_x[touch_state_index]=x;
			//	last_y[touch_state_index]=y;
			//   pendown = 1;
			
			} else if(point_status == 3 || point_status == 0) {
				//if(touch_state[touch_state_index] == TOUCH_DOWN){
					//ct360printk("%s:%d touch up\n",__FUNCTION__,i);
					input_mt_slot(ts->input_dev, point_id);
					input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
					input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
					syn_flag = 1;
					touch_state[touch_state_index] = TOUCH_UP;
					//printk("TOUCH_UP:%d\n", point_id);
		//		}
			}
		}
	}
	if(syn_flag)
		input_sync(ts->input_dev);
    
out:
	//printk("the gpio_get_value(ts->client->irq) is %d\n",gpio_get_value(ts->client->irq));
	//if(pendown==1)
	//{
	//	queue_delayed_work(ts->ct360_wq, &ts->work, msecs_to_jiffies(15));
		//pendown = 0;
	//}
	//else
//		enable_irq(ts->irq);
	
	return;
}

static irqreturn_t ct360_ts_irq_handler(int irq, void *dev_id)
{
    struct ct360_ts_data *ts = dev_id;
	//printk("the ts->irq is %d   ts->client->addr=%d\n",gpio_get_value(ts->irq),ts->client->addr);
//    disable_irq_nosync(ts->irq);
	
    queue_work(ts->ct360_wq, &ts->work);

    return IRQ_HANDLED;
}

static int ct360_chip_Init(struct i2c_client *client)
{
	int ret=0;
	u8 start_reg=0x00;
	unsigned char status;
	printk("enter ct360_chip_Init!!!!\n");
	u8 buf0[2];
	buf0[0] = 0xA5;
	client->addr = 0x01;
	ret = i2c_master_reg8_send(client, start_reg, buf0, 1, 200*1000);
	msleep(10);
	u8 buf1[2];
	buf1[0] = 0x00;
	ret = i2c_master_reg8_send(client, start_reg, buf1, 1, 200*1000);
	msleep(2);
	if(ret<0){
		printk("\n--%s--Set Register values error !!!\n",__FUNCTION__);
	}
	i2c_master_reg8_recv(client,start_reg,&status,1,200*1000);
	printk("the status is %x",status);
	if(status != 0xAA)
   	{
	   printk("the status11 is %x",status);
   	}
	
	return ret;
}

static int ct360_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ct360_ts_data *ts;
	struct ct360_platform_data	*pdata = client->dev.platform_data;
    int ret = 0;
	char loader_buf[3] = {0xfF,0x0f,0x2A};
	char boot_buf = 0;
	char boot_loader[2] = {0};

    ct360printk("%s \n",__FUNCTION__);
	
    if (!pdata) {
		dev_err(&client->dev, "empty platform_data\n");
		goto err_check_functionality_failed;
    }
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        printk(KERN_ERR "ct360_ts_probe: need I2C_FUNC_I2C\n");
        ret = -ENODEV;
        goto err_check_functionality_failed;
    }
	
    ts = kzalloc(sizeof(*ts), GFP_KERNEL);
    if (ts == NULL) {
        ret = -ENOMEM;
        goto err_alloc_data_failed;
    }

    ts->ct360_wq = create_singlethread_workqueue("ct360_wq");
    if (!ts->ct360_wq){
		printk(KERN_ERR"%s: create workqueue failed\n", __func__);
		ret = -ENOMEM;
		goto err_input_dev_alloc_failed;
	}

    INIT_WORK(&ts->work, ct360_ts_work_func);
    ts->client = client;
    i2c_set_clientdata(client, ts);
	
	if(pdata->hw_init)
		pdata->hw_init();

	if(pdata->shutdown){
		pdata->shutdown(1);
		mdelay(5);
		pdata->shutdown(0);
		mdelay(20);
		pdata->shutdown(1);
		mdelay(20);
	}
	
#if 0

	//加40ms延时，否则读取出错。。
	mdelay(20);
	mdelay(20);
	ret=ct360_write_regs(client,loader_buf, 3);	
	if(ret<0){
		printk("\n--%s--Set Register values error !!!\n",__FUNCTION__);
	}

	mdelay(1);
	printk("%s...........%d\n",__FUNCTION__,boot_buf);
	ret = i2c_master_normal_send(client,boot_loader,1,100*1000);
	if(ret < 0)
		printk("ct360_ts_probe:sdf  i2c_transfer fail =%d\n",ret);
	else
		printk("%s.............ok\n",__FUNCTION__);	

	mdelay(2);
	ret = ct360_read_regs(client,&boot_buf,1);
	printk("%s....3......%x\n",__FUNCTION__,boot_buf);
	
	if(ret < 0)
		printk("ct360_ts_probe:i2c_transfer fail =%d\n",ret);
	else
		printk("%s.............boot_buf=%d\n",__FUNCTION__,boot_buf);

	if ((abs(Binary_Data[16372]-boot_buf) >= 1) && (abs(Binary_Data[16372]-boot_buf) <= 2))
	{
		printk("start Bootloader ...........boot_Buf=%x.....%d......%x..........TP \n\n",boot_buf,(Binary_Data[16372]-boot_buf),Binary_Data[16372]);
		ret = CT360_CTP_BootLoader(ts);
		if (ret == 1)
			printk("TP Bootloader success\n");
		else
			printk("TP Bootloader failed  ret=%d\n",ret);
		printk("stop Bootloader.................................TP \n\n");
	}
	else
	{
		printk("Don't need bootloader.skip it %x \n",Binary_Data[16372]);
	}
#endif
	ts->client->addr = 0x01;
	if(pdata->shutdown){
		pdata->shutdown(1);
		mdelay(5);
		pdata->shutdown(0);
		mdelay(20);
		pdata->shutdown(1);
		mdelay(30);
	}

	ts->client->addr = 0x01;
	//ret=ct360_chip_Init(ts->client);
	//if(ret<0)
	//{
	//	printk("%s:chips init failed\n",__FUNCTION__);
	//	goto err_input_dev_alloc_failed;
	//}


	
    /* allocate input device */
    ts->input_dev = input_allocate_device();
    if (ts->input_dev == NULL) {
        ret = -ENOMEM;
        printk(KERN_ERR "%s: Failed to allocate input device\n",__FUNCTION__);
        goto err_input_dev_alloc_failed;
    }
	
	ts->x_max = pdata->x_max;
	ts->y_max = pdata->y_max;
	ts->swap_xy = 1;
	ts->input_dev->name = ct360_TS_NAME;
	ts->input_dev->dev.parent = &client->dev;

    __set_bit(EV_ABS, ts->input_dev->evbit);
    __set_bit(EV_KEY, ts->input_dev->evbit);
    __set_bit(EV_REP,  ts->input_dev->evbit);
    __set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
    set_bit(ABS_MT_POSITION_X, ts->input_dev->absbit);
    set_bit(ABS_MT_POSITION_Y, ts->input_dev->absbit);
    set_bit(ABS_MT_TOUCH_MAJOR, ts->input_dev->absbit);
    set_bit(ABS_MT_WIDTH_MAJOR, ts->input_dev->absbit);
    
    input_mt_init_slots(ts->input_dev, 5);    
    input_set_abs_params(ts->input_dev,ABS_MT_POSITION_X, 0, ts->x_max, 0, 0);
    input_set_abs_params(ts->input_dev,ABS_MT_POSITION_Y, 0, ts->y_max, 0, 0);
    input_set_abs_params(ts->input_dev,ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev,ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);

    ret = input_register_device(ts->input_dev);
    if (ret) {
        printk(KERN_ERR "%s: Unable to register %s input device\n", __FUNCTION__,ts->input_dev->name);
        goto err_input_register_device_failed;
    }

	ts->irq = gpio_to_irq(client->irq);
	ret = request_irq(ts->irq, ct360_ts_irq_handler, IRQF_TRIGGER_FALLING, client->name, ts);
	if (ret){
		printk("!!! ct360 request_irq failed\n");
		goto err_input_register_device_failed;
	}
	
#ifdef CONFIG_HAS_EARLYSUSPEND
    ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    ts->early_suspend.suspend = ct360_ts_early_suspend;
    ts->early_suspend.resume = ct360_ts_late_resume;
    register_early_suspend(&ts->early_suspend);
#endif
    printk(KERN_INFO "%s: probe ok  ts->client->addr=%d!!\n", __FUNCTION__,ts->client->addr);

    return 0;

err_input_register_device_failed:
    input_free_device(ts->input_dev);
err_input_dev_alloc_failed:
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	
    return ret;
}

static int ct360_ts_remove(struct i2c_client *client)
{
    struct ct360_ts_data *ts = i2c_get_clientdata(client);
    unregister_early_suspend(&ts->early_suspend);
	free_irq(ts->irq, ts);
    input_unregister_device(ts->input_dev);
    if (ts->ct360_wq)
        destroy_workqueue(ts->ct360_wq);

    kfree(ts);
    return 0;
}

static int ct360_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
    struct ct360_ts_data *ts = i2c_get_clientdata(client);
    struct ct360_platform_data	*pdata = client->dev.platform_data;
	int ret,i;
    printk("ct360 TS Suspend\n");
	//if(pdata->shutdown)
	//	pdata->shutdown(1);
	
    disable_irq(ts->irq);
    cancel_work_sync(&ts->work);
	
	char buf[3] = {0xff,0x0f,0x2b};
	char buf1[2]={0x00,0x00};
	//for(i=0;i<3;i++)
	//{
		ret = i2c_master_normal_send(client,buf,3,100*1000);
		if(ret<0)
		{
			printk("ct360_ts supend fail!\n");
		}
	//}
	mdelay(1);
	ret = i2c_master_normal_send(client,buf1,2,100*1000);
	if(ret<0)
	{
		printk("ct360_ts supend fail!!!\n");
	}

	//printk("the buf1 is %x\n",buf[0]);
	//gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_LOW:GPIO_HIGH);
	
    return 0;
}

static int ct360_ts_resume(struct i2c_client *client)
{
    struct ct360_ts_data *ts = i2c_get_clientdata(client);
    struct ct360_platform_data	*pdata = client->dev.platform_data;
   // if(pdata->shutdown)
	//	pdata->shutdown(0);
	
    //ct360_init_panel(ts);
    
    printk("ct360 TS Resume\n");
	if(pdata->shutdown){
		pdata->shutdown(1);
		mdelay(5);
		pdata->shutdown(0);
		mdelay(20);
		pdata->shutdown(1);
		mdelay(5);
	}
//	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);
//	msleep(50);

	//printk("enabling IRQ %d\n", ts->irq);
	enable_irq(ts->irq);
	msleep(50);
    return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ct360_ts_early_suspend(struct early_suspend *h)
{
	//#if 1
    struct ct360_ts_data *ts;
   //printk("======%s======\n",__FUNCTION__);
    ts = container_of(h, struct ct360_ts_data, early_suspend);
    ct360_ts_suspend(ts->client, PMSG_SUSPEND);
	//#endif
}

static void ct360_ts_late_resume(struct early_suspend *h)
{
	#if 1
    struct ct360_ts_data *ts;
    ts = container_of(h, struct ct360_ts_data, early_suspend);
    ct360_ts_resume(ts->client);
	#endif
}
#endif

static const struct i2c_device_id ct360_ts_id[] = {
    { ct360_TS_NAME, 0 },
    { }
};

static struct i2c_driver ct360_ts_driver = {
    .probe      = ct360_ts_probe,
    .remove     = ct360_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
    .suspend    = ct360_ts_suspend,
    .resume     = ct360_ts_resume,
#endif
    .id_table   = ct360_ts_id,
    .driver = {
        .name   = ct360_TS_NAME,
    },
};

static int __devinit ct360_ts_init(void)
{
    printk("%s\n",__FUNCTION__);

    return i2c_add_driver(&ct360_ts_driver);
}

static void __exit ct360_ts_exit(void)
{
    printk("%s\n",__FUNCTION__);
    i2c_del_driver(&ct360_ts_driver);
}

late_initcall_sync(ct360_ts_init);
module_exit(ct360_ts_exit);

MODULE_DESCRIPTION("ct360 Touchscreen Driver");
MODULE_LICENSE("GPL");
