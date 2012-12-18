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
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/platform_device.h>


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

#define IOMUX_NAME_SIZE 48

enum regadd {
	ptxh = 0, ptxl = 1, ptyh = 2, ptyl = 3, ptpressure = 4,
};
enum touchstate {
	TOUCH_UP = 0, TOUCH_DOWN = 1,
};


#define TOUCH_NUMBER 10
#define TOUCH_REG_NUM 6 

#define ct360_TS_NAME "ct3610_ts"

struct ct360_ts_data {
	u16		x_max;	
	u16		y_max;
	bool	swap_xy;           //define?
	int 	irq;
	struct 	i2c_client *client;
    struct 	input_dev *input_dev;
	struct workqueue_struct *ct360_wq;
    struct 	work_struct  work;
    struct 	early_suspend early_suspend;
};
/*tochscreen private data*/
static int touch_state[TOUCH_NUMBER] = {TOUCH_UP,TOUCH_UP};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ct360_ts_early_suspend(struct early_suspend *h);
static void ct360_ts_late_resume(struct early_suspend *h);
#endif

/*read the ct360 register ,used i2c bus*/
static int ct360_read_regs(struct i2c_client *client, u8 buf[], unsigned len)
{
	int ret;
	ret =i2c_master_normal_recv(client, buf, len, 400*1000);
	if(ret < 0)
		printk("ct360_ts_work_func:i2c_transfer fail =%d\n",ret);
	return ret;
}
/* set the ct360 registe,used i2c bus*/
static int ct360_write_regs(struct i2c_client *client, u8 reg, u8 const buf[], unsigned short len)
{
	int ret;
	ret = i2c_master_reg8_send(client,reg, buf, len, 100*1000);
 	if (ret < 0) {
	  printk("ct360_ts_work_func:i2c_transfer fail =%d\n",ret);
    }
	return ret;
}

char const  Binary_Data[32768]=
{
//#include "CT365RC972030D_V39120329A_waterproof_1.dat"
#include "CT36X_JS_DS_973H_LX20x30_V18120810W.txt"
};

char CTP_BootLoader(struct ct360_ts_data *ts)
{
	unsigned int i = 0 ; 
	unsigned int j = 0 ;  
	unsigned int version = 0;
	char value = 0;
	char I2C_Buf[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	unsigned int Flash_Address = 0 ;				
	char CheckSum[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};  		// 128/8 = 16 times 

	//--------------------------------------
	// Step1 --> initial BootLoader
	// Note. 0x7F -> 0x00 -> 0xA5 ;
	//--------------------------------------
	I2C_Buf [0] = 0x00;	
	I2C_Buf [1] = 0xA5;			

	ts->client->addr = 0x7F;
	ct360_write_regs(ts->client,0x00,&I2C_Buf[1], 1);					// Host issue 0xA5 Command to CT365  
	mdelay(3);										// Delay 1 ms 


	//------------------------------
	// Reset I2C Offset address
	// Note. 0x7F -> 0x00   
	//------------------------------
	I2C_Buf [0] = 0x00 ;		
	i2c_master_normal_send(ts->client,I2C_Buf, 1,100*1000);					// Reset CT365 I2C Offset address   
	udelay(500);									// Delay 500 us 	

	//------------------------------
	// Read I2C Bus status
	//------------------------------
	i2c_master_normal_recv(ts->client,&value, 1,100*1000);

	boot_printk("%s......0...\n",__FUNCTION__);
	// if return "AAH" then going next step
	if (value != 0xAA)
		return 0;		

	boot_printk("%s......1...\n",__FUNCTION__);

	{
		I2C_Buf[0] = 0x00;
		I2C_Buf[1] = 0x99;														// Generate check sum command
		I2C_Buf[2] = (char)(0x0044 >> 8);			// define a flash address for CT365 to generate check sum	
		I2C_Buf[3] = (char)(0x0044 & 0xFF);		//
		I2C_Buf[4] = 0x08;														// Define a data length for CT365 to generate check sum	

		i2c_master_normal_send(ts->client,I2C_Buf, 5,100*1000);								// Write Genertate check sum command to CT365

		mdelay(1);																	// Delay 1 ms

		i2c_master_reg8_recv(ts->client,0x00, I2C_Buf,13, 100*1000);					// Read check sum and flash data from CT365
		if (!(I2C_Buf[5] != 'V') || (I2C_Buf[9] != 'T'))
			version = 1;		
	}	  

	{
		I2C_Buf[0] = 0x00;
		I2C_Buf[1] = 0x99;														// Generate check sum command
		I2C_Buf[2] = (char)(0x00a4 >> 8);			// define a flash address for CT365 to generate check sum	
		I2C_Buf[3] = (char)(0x00a4 & 0xFF);		//
		I2C_Buf[4] = 0x08;														// Define a data length for CT365 to generate check sum	

		i2c_master_normal_send(ts->client,I2C_Buf, 5,100*1000);								// Write Genertate check sum command to CT365

		mdelay(1);																	// Delay 1 ms

		i2c_master_reg8_recv(ts->client,0x00, I2C_Buf,13, 100*1000);					// Read check sum and flash data from CT365
		if (!(I2C_Buf[5] != 'V') || (I2C_Buf[9] != 'T'))
			version = 2;		
	}	

	if (!version)
		return 0;

	//------------------------------
	// Reset I2C Offset address
	// Note. 0x7F -> 0x00   
	//------------------------------
	I2C_Buf [0] = 0x00;	
	I2C_Buf [1] = 0xA5;			

	i2c_master_normal_send(ts->client,I2C_Buf, 2,100*1000);					// Host issue 0xA5 Command to CT365  
	mdelay(3);

	I2C_Buf [0] = 0x00 ;		
	i2c_master_normal_send(ts->client,I2C_Buf, 1,100*1000);					// Reset CT365 I2C Offset address   
	udelay(500);		
	//-----------------------------------------------------
	// Step 2 : Erase 32K flash memory via Mass Erase (33H)  
	// 0x7F --> 0x00 --> 0x33 --> 0x00 ; 
	//-----------------------------------------------------
	I2C_Buf [0] = 0x00;												// Offset address 
	I2C_Buf [1] = 0x33;												// Mass Erase command
	I2C_Buf [2] = 0x00;  

	i2c_master_normal_send(ts->client,I2C_Buf,3,100*1000);
	mdelay(10);													// Delay 10 mS


	//------------------------------
	// Reset I2C Offset address
	// Note. 0x7F -> 0x00   
	//------------------------------
	I2C_Buf [0] = 0x00 ;		
	i2c_master_normal_send(ts->client,I2C_Buf, 1,100*1000);						// Reset CT365 I2C Offset address   
	udelay(500);													// Delay 500 us 	


	//------------------------------
	// Read I2C Bus status
	//------------------------------
	i2c_master_normal_recv(ts->client,&value, 1,100*1000);

	// if return "AAH" then going next step
	if (value != 0xAA)
		return 0;		

	boot_printk("%s......2...\n",__FUNCTION__);

	//----------------------------------------
	// Step3. Host write 128 bytes to CT365  
	// Step4. Host read checksum to verify ;
	// Write/Read for 256 times ( 32k Bytes )
	//----------------------------------------

	for ( j = 0 ; j < 256 ; j++ )							// 32k/128 = 256 times 
	{
		Flash_Address = 128*j ; 							// 0 ~ 127 ; 128 ~ 255 ; 

		for ( i = 0 ; i < 16 ; i++ )					// 128/8 = 16 times for One Row program 
		{
			// Step 3 : write binary data to CT365  
			I2C_Buf[0] = 0x00;															// Offset address 
			I2C_Buf[1] = 0x55;															// Flash write command
			I2C_Buf[2] = (char)(Flash_Address  >> 8);			// Flash address [15:8]
			I2C_Buf[3] = (char)(Flash_Address & 0xFF);			// Flash address [7:0]
			I2C_Buf[4] = 0x08;								// Data Length 
			I2C_Buf[6] = Binary_Data[Flash_Address + 0];		// Binary data 1
			I2C_Buf[7] = Binary_Data[Flash_Address + 1];		// Binary data 2
			I2C_Buf[8] = Binary_Data[Flash_Address + 2];		// Binary data 3
			I2C_Buf[9] = Binary_Data[Flash_Address + 3];		// Binary data 4
			I2C_Buf[10] = Binary_Data[Flash_Address + 4]; 		// Binary data 5
			I2C_Buf[11] = Binary_Data[Flash_Address + 5];		// Binary data 6
			I2C_Buf[12] = Binary_Data[Flash_Address + 6];		// Binary data 7
			I2C_Buf[13] = Binary_Data[Flash_Address + 7];		// Binary data 8

			// Calculate a check sum by Host controller. 
			// Checksum = / (FLASH_ADRH+FLASH_ADRL+LENGTH+
			// Binary_Data1+Binary_Data2+Binary_Data3+Binary_Data4+
			// Binary_Data5+Binary_Data6+Binary_Data7+Binary_Data8) + 1 
			CheckSum[i] = ~(I2C_Buf[2] + I2C_Buf[3] + I2C_Buf[4] + I2C_Buf[6] + I2C_Buf[7] + 
			I2C_Buf[8] + I2C_Buf[9] + I2C_Buf[10] + I2C_Buf[11] + I2C_Buf[12] +
			I2C_Buf[13]) + 1; 

			I2C_Buf[5] = CheckSum[i];										// Load check sum to I2C Buffer 

			i2c_master_normal_send(ts->client,I2C_Buf, 14,100*1000);									// Host write I2C_Buf[012] to CT365. 

			mdelay(1);													// 8 Bytes program --> Need 1 ms delay time 

			Flash_Address += 8 ;											// Increase Flash Address. 8 bytes for 1 time 

		}

		mdelay(20);																// Each Row command --> Need 20 ms delay time 

		Flash_Address = 128*j ; 										// 0 ~ 127 ; 128 ~ 255 ; 

		// Step4. Verify process 
		for ( i = 0 ; i < 16 ; i++ )										// 128/8 = 16 times for One Row program 
		{
			//Step 4 : Force CT365 generate check sum for host to compare data. 
			//Prepare get check sum from CT365
			I2C_Buf[0] = 0x00;
			I2C_Buf[1] = 0x99;														// Generate check sum command
			I2C_Buf[2] = (char)(Flash_Address >> 8);			// define a flash address for NT1100x to generate check sum	
			I2C_Buf[3] = (char)(Flash_Address & 0xFF);		//
			I2C_Buf[4] = 0x08;														// Define a data length for CT36x to generate check sum	

			i2c_master_normal_send(ts->client,I2C_Buf, 5,100*1000);									// Write Genertate check sum command to CT365

			mdelay(1);																	// Delay 1 ms

			i2c_master_reg8_recv(ts->client,0x00,I2C_Buf, 13,100*1000);						// Read check sum and flash data from CT365

			// Compare host check sum with CT365 check sum(I2C_Buf[4])
			if ( I2C_Buf[4] != CheckSum[i] )
			{
				boot_printk("%s......3...\n",__FUNCTION__);
				return 0;
			}

			Flash_Address += 8;														// Increase Flash Address.

		}

	} 	

	ts->client->addr = 0x01;
	boot_printk("%s......4...\n",__FUNCTION__);
	return  1 ; 

}

static void ct360_ts_work_func(struct work_struct *work)
{
	
	unsigned short x;
	unsigned short y;
	int i,ret,syn_flag=0;
	char toatl_num = 0;
	unsigned char buf[TOUCH_REG_NUM*TOUCH_NUMBER+1] = {0};
	int point_status;
	int point_num,point_pressure;
	
	struct ct360_ts_data *ts = container_of(work,struct ct360_ts_data,work);

	ret= ct360_read_regs(ts->client,buf,60);//only one data  represent the current touch num
	if (ret < 0) {
	  	printk("%s:i2c_transfer fail =%d\n",__FUNCTION__,toatl_num);
		goto out;
		return;
    }

    for (i=0; i<60; i+=TOUCH_REG_NUM)
    {
		if (((buf[i+1] << 4)|(buf[i+3] >> 4)) != 0x0fff)
		{
			x = ((buf[i+1] << 4)|(buf[i+3] >> 4));
			y = ((buf[i+2] << 4)|(buf[i+3]&0x0F));
			point_status=buf[i]&0x07;
			point_num = buf[i]>>3;
			point_pressure = (buf[i+5]*4)>=255? 255 : (buf[i+5]*4);

			if((point_status == 1) || (point_status == 2)){
				input_mt_slot(ts->input_dev, point_num-1);
				input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, point_num-1);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, point_pressure); //Finger Size
				input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, point_pressure); //Touch Size
				//input_mt_sync(ts->input_dev);
				syn_flag = 1;
				touch_state[point_num-1] = TOUCH_DOWN;
			}
			else if(point_status == 3){
				input_mt_slot(ts->input_dev, point_num-1);
				input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
				touch_state[point_num-1] = TOUCH_UP;
				syn_flag =1;
			}
		}
	}
	
	if(syn_flag){
		input_sync(ts->input_dev);
    }

out:
   	enable_irq(ts->irq);
	return;
}

static irqreturn_t ct360_ts_irq_handler(int irq, void *dev_id)
{
    struct ct360_ts_data *ts = dev_id;
    
	disable_irq_nosync(ts->irq);
	queue_work(ts->ct360_wq, &ts->work);

    return IRQ_HANDLED;
}

static int ct360_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ct360_ts_data *ts;
	struct ct360_platform_data	*pdata = client->dev.platform_data;
    	int ret = 0;
	char loader_buf[2] = {0x3f,0xff};
	char boot_buf = 0;
	char boot_loader[2] = {0};
    printk("%s \n",__FUNCTION__);
	
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
		mdelay(200);
		pdata->shutdown(0);
		mdelay(50);
		pdata->shutdown(1);
		mdelay(50);
	}
	//加40ms延时，否则读取出错。。
	mdelay(40);
	ret=ct360_write_regs(client,0xfF, loader_buf, 2);	
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

	if (Binary_Data[32756] != boot_buf)
	{
		printk("start Bootloader ...........boot_Buf=%x.....%d......%x..........TP \n\n",boot_buf,(Binary_Data[16372]-boot_buf),Binary_Data[16372]);
		ret = CTP_BootLoader(ts);
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

	if(pdata->shutdown){
		pdata->shutdown(1);
		mdelay(5);
		pdata->shutdown(0);
		mdelay(20);
		pdata->shutdown(1);
		mdelay(20);
	}
	ts->client->addr = 0x01;
	
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

	//set_bit(EV_SYN, ts->input_dev->evbit);
	//set_bit(BTN_TOUCH, ts->input_dev->keybit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	//set_bit(BTN_2, ts->input_dev->keybit);

	input_mt_init_slots(ts->input_dev, TOUCH_NUMBER);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->x_max, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->y_max, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0); //Finger Size
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0); //Touch Size
    input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 10, 0, 0); //Touch Size

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

    printk("%s: probe ok!!\n", __FUNCTION__);

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

	int ret,i;
	char buf[3] = {0xff,0x8f,0xff};
	
	//disable_irq(ts->irq);
    free_irq(ts->irq,ts);
    cancel_work_sync(&ts->work);
	flush_work(&ts->work);

	ret = i2c_master_normal_send(client, buf, 3, 400*1000);
 	if (ret < 0) {
	  printk("ct360_ts_suspend:i2c_transfer fail 1=%d\n",ret);
    }
	msleep(3);
	buf[0] = 0x00;
	buf[1] = 0xaf;
	ret = i2c_master_normal_send(client, buf, 2, 400*1000);
 	if (ret < 0) {
	  printk("ct360_ts_suspend:i2c_transfer fail 2=%d\n",ret);
    }	

	for (i =0; i<10; i++)
	{
		if (touch_state[i] == TOUCH_DOWN)
		{
			input_mt_slot(ts->input_dev, i);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
			touch_state[i] = TOUCH_UP;
		}
	}
    return ret;
}

static int ct360_ts_resume(struct i2c_client *client)
{
    struct ct360_ts_data *ts = i2c_get_clientdata(client);
    struct ct360_platform_data	*pdata = client->dev.platform_data;
	int i ,ret = 0;

    if(pdata->shutdown)
	{	pdata->shutdown(1);
		mdelay(200);
		pdata->shutdown(0);
		mdelay(50);
		pdata->shutdown(1);
		mdelay(50);
	}
    printk("ct360 TS Resume\n");
    
      for(i=0; i<10; i++) {
            {
                input_mt_slot(ts->input_dev, i);
                input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
            }
    }

	for(i=0; i<10; i++) {
            {
                input_mt_slot(ts->input_dev, i);
                input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
            }
    }
	
    input_sync(ts->input_dev);

	ret = request_irq(ts->irq, ct360_ts_irq_handler, IRQF_TRIGGER_FALLING, client->name, ts);
	if (ret){
		printk("!!! ct360 request_irq failed\n");
	}
    return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ct360_ts_early_suspend(struct early_suspend *h)
{
    struct ct360_ts_data *ts;
    ts = container_of(h, struct ct360_ts_data, early_suspend);
    ct360_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void ct360_ts_late_resume(struct early_suspend *h)
{
    struct ct360_ts_data *ts;
    ts = container_of(h, struct ct360_ts_data, early_suspend);
    ct360_ts_resume(ts->client);
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

module_init(ct360_ts_init);
module_exit(ct360_ts_exit);

MODULE_DESCRIPTION("ct360 Touchscreen Driver");
MODULE_LICENSE("GPL");
