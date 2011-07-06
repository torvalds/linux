/* 
 * drivers/input/touchscreen/ft5x0x_ts.c
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

#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/delay.h>
//#include <arch/api_intc.h>
//#include <arch/typedef.h>
//#include <arch/gpio.h>
//#include <arch/api_intc.h>
//#include <arch/hw_define.h>
//#include <arch/hardware.h>
//#include <arch/gpio.h>
//#include <arch/iomux.h>
//#include <asm-arm/uaccess.h>
#include <linux/ioport.h>
#include <linux/input-polldev.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#ifdef CONFIG_ANDROID_POWER
#include <linux/android_power.h>
#endif


#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <mach/rk29_iomap.h>
#include <mach/board.h>
#include <mach/rk29_nand.h>
#include <mach/rk29_camera.h>                          /* ddl@rock-chips.com : camera support */
#include <media/soc_camera.h>                               /* ddl@rock-chips.com : camera support */
#include <mach/vpu_mem.h>
#include <mach/sram.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

//#include <asm/arch/api_i2c.h>
#include "ft5406_ts.h"


#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend ft5406_early_suspend;
#endif

static struct i2c_client *this_client;



#define CONFIG_FT5X0X_MULTITOUCH  1
#define TOUCH_RESET_PIN RK29_PIN6_PC3

struct ts_event {
	u16	x1;
	u16	y1;
	u16	x2;
	u16	y2;
	u16	x3;
	u16	y3;
	u16	x4;
	u16	y4;
	u16	x5;
	u16	y5;
	u16	pressure;
       s16  touch_ID1;
	s16  touch_ID2;
       s16  touch_ID3;
       s16  touch_ID4;
	s16  touch_ID5;
	u8   touch_point;
	u8   status;
};

struct ft5x0x_ts_data {
	struct i2c_client *client;
	struct input_dev	*input_dev;
	int 		irq;
	struct ts_event		event;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
};

static int fts_ctpm_fw_upgrade_with_i_file(void);



//#define FT5X0X_I2C_ADDR   	0x70
#define MAX_POINT 5
/*
static int ft5x0x_ts_probe(struct i2c_adapter *bus, int address, int kind);
static unsigned short ft5x0x_normal_i2c[] = {FT5X0X_I2C_ADDR>>1, I2C_CLIENT_END};
static unsigned short ft5x0x_ignore = I2C_CLIENT_END;
static struct i2c_client_address_data ft5x0x_addr_data={
	.normal_i2c = ft5x0x_normal_i2c,
	.probe = &ft5x0x_ignore,
	.ignore =&ft5x0x_ignore,
};

static int ft5x0x_attach_adapter(struct i2c_adapter *adap)
{
	return i2c_probe(adap, &ft5x0x_addr_data, ft5x0x_ts_probe);
}

static struct i2c_driver ft5x0x_ts_driver = {
	.driver	= {
		.name	= FT5X0X_NAME,
		.owner	= THIS_MODULE,
	},
	.id = FT5X0X_I2C_ADDR,
	.attach_adapter = &ft5x0x_attach_adapter,
};




static struct  i2c_client ft5x0x_client = {
	.driver = &ft5x0x_ts_driver,
	.name	= "ft5x0x",
};
*/
/*read the it7260 register ,used i2c bus*/

#define FT5406_IIC_SPEED   200*1000    //300*1000
static int ft5406_read_regs(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, FT5406_IIC_SPEED);
	return ret; 
}


/* set the it7260 registe,used i2c bus*/
static int ft5406_set_regs(struct i2c_client *client, u8 reg, u8 const buf[], unsigned short len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, FT5406_IIC_SPEED);
	return ret;
}




/*
static int ft5x0x_i2c_rxdata(u8 reg, u8 rxdata[], int length)
{
	int ret;
	struct i2c_msg msg[1];
	
    msg->addr = ft5x0x_client.addr;
    msg->flags |= I2C_M_RD;
    msg->buf = rxdata;
    msg->len = length;

	//printk("ft50x0_client.addr = 0x%x\n", ft5x0x_client.addr);
	rxdata[0] = reg;

	ret = i2c_transfer(ft5x0x_client.adapter, msg, 1);
	if (ret< 0)
	{
		printk("error at ft5x0x_read_regs !!! \n");	
	}
	return ret;

}

static int ft5x0x_i2c_txdata(u8 reg, u8 txdata[], int length)
{
	int ret;
	struct i2c_msg msg[1];
	static u8 i2c_buf[128];
	
    msg->addr = ft5x0x_client.addr;
    msg->flags = 0;
    msg->buf = i2c_buf;
    msg->len = length + 1;

	
	i2c_buf[0] = reg;
	memcpy(&i2c_buf[1], &txdata[0], length);	
	ret = i2c_transfer(ft5x0x_client.adapter, msg, 1);
	if (ret< 0)
	{
		printk("error at gt800_write_regs !!! \n");	
	}	
	return ret;

}

*/
static void ft5x0x_ts_release(struct ft5x0x_ts_data *data)
{
	//struct ft5x0x_ts_data *data = i2c_get_clientdata(&ft5x0x_client);
	//struct ts_event *event = &data->event;

#if   CONFIG_FT5X0X_MULTITOUCH	       //  #ifdef
	input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
	input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 0);
#else
	input_report_abs(data->input_dev, ABS_PRESSURE, 0);
	input_report_key(data->input_dev, BTN_TOUCH, 0);
#endif
	input_sync(data->input_dev);
}

static int ft5x0x_read_data(struct ft5x0x_ts_data *data )
{
	//struct ft5x0x_ts_data *data = i2c_get_clientdata(&ft5x0x_client);
	struct ts_event *event = &data->event;
	u8 start_reg=0x0;
	u8 buf[32] = {0};
	int ret = -1;
	//int i = 0;
	int status = 0;
	int cnt = 0;

#if 0
	start_reg = 0xa6;
	ret = ft5x0x_i2c_rxdata(start_reg, buf, 2);
    if (ret < 0) {
		printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}
	for (i=0; i<2; i++) {
		printk("=========buf[%d] = 0x%x \n", i, buf[i]);
	}
#endif
	

	start_reg = 0;


   
#if CONFIG_FT5X0X_MULTITOUCH              //   #ifdef 
	if (MAX_POINT == 5) {
		ret = ft5406_read_regs(data->client,start_reg, buf, 31);
	} else {
		ret = ft5406_read_regs(data->client,start_reg, buf, 13);
	}
#else
    ret = ft5406_read_regs(data->client,start_reg, buf, 7);
#endif
    if (ret < 0) {
	//	printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}
#if 0
	for (i=0; i<32; i++) {
		printk("buf[%d] = 0x%x \n", i, buf[i]);
	}
#endif

	memset(event, 0, sizeof(struct ts_event));

	if (MAX_POINT == 5) {
		event->touch_point = buf[2] & 0x07;// 000 0111
	} else {
		event->touch_point = buf[2] & 0x03;// 0000 0011
	}

//	printk("touch_point = %d\n", event->touch_point);
    if (event->touch_point == 0) {
	//	printk("release point !!!!!!!!!!!!!!!!!\n");
		ft5x0x_ts_release(data);
		return 1; 
    }	
	cnt =0;
#if CONFIG_FT5X0X_MULTITOUCH               //  #ifdef
    switch (event->touch_point) {
		if (MAX_POINT == 5)	{
			case 5:
				event->x5 = (s16)(buf[0x1b] & 0x0F)<<8 | (s16)buf[0x1c];
				event->y5 = (s16)(buf[0x1d] & 0x0F)<<8 | (s16)buf[0x1e];
				status = (s16)((buf[0x1b] & 0xc0) >> 6);
				event->touch_ID5=(s16)(buf[0x1D] & 0xF0)>>4;
				if(event->x5 > 1024)
					cnt ++;
				//   printk("read ID5 = %d\n",event->touch_ID5,status);
				//	printk("read status5= %d\n",status);
			#if 0		
				if (status == 1) {
						printk("point 5 release!\n");
					ft5x0x_ts_release(data);
				}
			#endif	
			case 4:
				event->x4 = (s16)(buf[0x15] & 0x0F)<<8 | (s16)buf[0x16];
				event->y4 = (s16)(buf[0x17] & 0x0F)<<8 | (s16)buf[0x18];
				status = (s16)((buf[0x15] & 0xc0) >> 6);
				//event->touch_ID4=(s16)(buf[0x17] & 0xF0);
				event->touch_ID4=(s16)(buf[0x17] & 0xF0)>>4;
			//	printk("read ID4 = %d,read status4 = %d\n",event->touch_ID4,status);
			#if 0
				if (status == 1) {
						printk("point 4 release!\n");
					ft5x0x_ts_release(data);
				}
			#endif	
				if(event->x4 >1024)
					cnt ++;
			case 3:
				event->x3 = (s16)(buf[0x0f] & 0x0F)<<8 | (s16)buf[0x10];
				event->y3 = (s16)(buf[0x11] & 0x0F)<<8 | (s16)buf[0x12];
				status = (s16)((buf[0x0f] & 0xc0) >> 6);
			//	event->touch_ID3=(s16)(buf[0x11] & 0xF0);
				event->touch_ID3=(s16)(buf[0x11] & 0xF0)>>4;
			//		printk("read ID3 = %d,read status3 = %d\n",event->touch_ID3,status);
			#if 0
				if (status == 1) {
						printk("point 3 release!\n");
					ft5x0x_ts_release(data);
				}
			#endif	
				if(event->x3 > 1024)
					cnt ++;
		}
		case 2:
			event->x2 = (s16)(buf[9] & 0x0F)<<8 | (s16)buf[10];
			event->y2 = (s16)(buf[11] & 0x0F)<<8 | (s16)buf[12];
			status = (s16)((buf[0x9] & 0xc0) >> 6);
		    //   event->touch_ID2=(s16)(buf[0x0b] & 0xF0);
		    event->touch_ID2=(s16)(buf[0x0b] & 0xF0)>>4;
		//	printk("read ID2 = %d,read status2 = %d\n",event->touch_ID2,status);
		#if 0
			if (status == 1) {
					printk("point 2 release!\n");
				ft5x0x_ts_release(data);
			}
		#endif	
			if(event->x2 > 1024)
				cnt ++;
		case 1:
			event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
			event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
			status = ((buf[0x3] & 0xc0) >> 6);
			event->status= status ;
                   //  event->touch_ID1=(s16)(buf[5] & 0xF0);
			event->touch_ID1=(s16)(buf[0x05] & 0xF0)>>4;
				//printk("status1= %d\n",event->status); 
		#if 0		
			if (status == 1) {
				printk("point 1 release!\n");
				ft5x0x_ts_release(data);
			}
		#endif
			if(event->x1 >1024)
				cnt ++;
		
            break;
		default:
		    return -1;
	}

/*
	if((event->touch_point ==1)
		&&((event->y1<BACK_KEY_MIN) ||(event->y2>MENU_KEY_MAX)))
	{
		ft5x0x_ts_release(data);
		return 1;
	}
	
	if(((event->touch_point - cnt) ==0)&&(event->touch_point >1))
	{
		ft5x0x_ts_release(data);
		return 1;
	}
*/
#else
    if (event->touch_point == 1) {
    	event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
		event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
    }
#endif
    event->pressure =200;         //200;

 //   printk("status2= %d\n",event->status); 
 
    return 0;
}

static void ft5x0x_report_value(struct ft5x0x_ts_data *data )
{
	//struct ft5x0x_ts_data *data = i2c_get_clientdata(&ft5x0x_client);
	struct ts_event *event = &data->event;

#if  CONFIG_FT5X0X_MULTITOUCH           //#ifdef
	switch(event->touch_point) {
		if (MAX_POINT == 5){
			case 5:

				if(event->x5 <= 1024)
				{
					input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID5);			
					input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
					input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x5);
					input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y5);
					input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
					input_mt_sync(data->input_dev);
				}
			//	printk("===x5 = %d,y5 = %d ====\n",event->x5,event->y5);
			case 4:
				if(event->x4 <= 1024)
				{
					input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID4);			
					input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
					input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x4);
					input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y4);
					input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
					input_mt_sync(data->input_dev);
				}
			//	printk("===x4 = %d,y4 = %d ====\n",event->x4, event->y4);
			case 3:
				if(event->x3 <= 1024)
				{
					input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID3);			
					input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
					input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x3);
					input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y3);
					input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
					input_mt_sync(data->input_dev);
				}
			//	printk("===x3 = %d,y3 = %d ====\n",event->x3, event->y3);
		}
		case 2:
			if(event->x2 <= 1024)
			{
				input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID2);			
				input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
				input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x2);
				input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y2);
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
				input_mt_sync(data->input_dev);
			}
		//	printk("===x2 = %d,y2 = %d ====\n",event->x2,event->y2);
		case 1:
		input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID1);			
		input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
		input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x1);
		input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y1);
		input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
		input_mt_sync(data->input_dev);
	//	printk("===x1 = %d,y1 = %d ====\n",event->x1,event->y1);
	default:
	//	printk("==touch_point default =\n");
	//	printk("read status0= %d\n",event->status); 
	//	if (event->status== 1) {
	//		printk("point 0 release!\n");
	//		ft5x0x_ts_release(data);
	//	}
	break;
	}
#else	/* CONFIG_FT5X0X_MULTITOUCH*/
	//if (event->touch_point == 1) {
		input_report_abs(data->input_dev, ABS_X, event->x1);
		input_report_abs(data->input_dev, ABS_Y, event->y1);
		input_report_abs(data->input_dev, ABS_PRESSURE, event->pressure);
	//}
	//printk("x = %d,y = %d\n",event->x1,event->y1);
	input_report_key(data->input_dev, BTN_TOUCH, 1);
#endif	/* CONFIG_FT5X0X_MULTITOUCH*/
	input_sync(data->input_dev);

 //   printk("status3= %d\n",event->status); 
 

}	/*end ft5x0x_report_value*/

static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
	int ret = -1;
//	printk("==work 1=\n");

	struct ft5x0x_ts_data *ft5x0x_ts =
		container_of(work, struct ft5x0x_ts_data, pen_event_work);

	
	ret = ft5x0x_read_data(ft5x0x_ts);	
	if (ret == 0) {	
		ft5x0x_report_value(ft5x0x_ts);
	//	printk("==work 2=\n");
	}
//	else printk("data package read error\n");
//	printk("==work 2=\n");
//    	msleep(1);
             enable_irq(ft5x0x_ts->irq);
	//enable_irq(7);
//	gpio_irq_enable(TOUCH_INT_IOPIN);
}

static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{

	struct ft5x0x_ts_data *ft5x0x_ts = dev_id;

	//printk("ft5x0x_ts irq  =%d",ft5x0x_ts->irq);

	      disable_irq_nosync(ft5x0x_ts->irq);
   //	disable_irq(ft5x0x_ts->irq);		
//	disable_irq(7);
	//gpio_irq_disable(ft5x0x_ts->irq);

	if (!work_pending(&ft5x0x_ts->pen_event_work)) 
	{
		queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
    //   printk("ft5x0x_ts_work!!!!!!!!!!!!!!!!!!!!!!!!!!\n");		
	}

	return IRQ_HANDLED;
}


static int __devexit ft5406_remove(struct i2c_client *client)
{
	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(client);

    // printk("==ft5x0x_ts_remove=\n");

//	free_irq(client->irq, ft5x0x_ts);
//	free_irq(7, ft5x0x_ts);              
	free_irq(ft5x0x_ts->irq, ft5x0x_ts);

	input_unregister_device(ft5x0x_ts->input_dev);
	kfree(ft5x0x_ts);
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ft5406_early_suspend);
#endif 
    this_client = NULL;

	return 0;
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static int ft5x0x_ts_suspend(struct early_suspend *h)
{

u8 buf[1];
//u8 buf_r[1];
int read_data=5;
int err = 0;

//	printk("==ft5x0x_ts_suspend=\n");

       
//	struct ft5x0x_ts_data *ts;
//	ts =  container_of(handler, struct ft5x0x_ts_data, early_suspend);
   #if 0
	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(client);
	if (device_may_wakeup(&client->dev))
		enable_irq_wake(ft5x0x_ts->irq);
   #endif
//	disable_irq(ft5x0x_client->irq);
//	cancel_work_sync(&ts->pen_event_work);
//	flush_workqueue(ts->ts_workqueue);
// ==set mode ==, 
           disable_irq(102);
	#if 1
	 buf[0]=PMODE_HIBERNATE;
	while(read_data)
	     {
   	   err=ft5406_set_regs(this_client,FT5X0X_REG_PMODE, buf,1);
	//      printk("==ft5406 suspend write 111=%d\n",buf[0]);
	   if(err==1)
	   	 {
	   	read_data=0;
	   	 }
	   else
	   	  {
	   	  read_data--;
	//   	printk("==ft5406 suspend write222=%d\n",buf[0]);  
	   ft5406_set_regs(this_client,FT5X0X_REG_PMODE, buf,1);	  
	   	  }
	     }
	
	//	printk("==ft5406 suspend write=%d\n",buf[0]);
	//	printk("==ft5406 suspend write err=%d\n",err);
	//       msleep(50);                             //harry 2011.04.20
	//       err = ft5406_read_regs(this_client,FT5X0X_REG_PMODE,buf_r,1);
	//       printk("==ft5406 suspend read=%d\n",buf_r[0]);
	//	printk("==ft5406 suspend read err=%d\n",err);

       #endif

		  
	return 0;

}


static int ft5x0x_ts_resume(struct early_suspend *h)
{
	//printk("==ft5x0x_ts_resume=\n");
          #if 0
		struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(client);
	if (device_may_wakeup(&client->dev))
		disable_irq_wake(ft5x0x_ts->irq);
          #endif
	// wake the mode
//	__gpio_as_output(GPIO_FT5X0X_WAKE);		
//	__gpio_clear_pin(GPIO_FT5X0X_WAKE);		//set wake = 0,base on system
//	 msleep(100);
//	__gpio_set_pin(GPIO_FT5X0X_WAKE);			//set wake = 1,base on system
//	msleep(100);
//	enable_irq(IRQ_EINT(6));
       enable_irq(102);
         #if 1
       gpio_direction_output(TOUCH_RESET_PIN, 0);
	gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
	msleep(20);                             //harry 2011.04.20
	gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
         #endif
		 
	return 0;
}
#endif

/***********************************************************************/

#define    FTS_PACKET_LENGTH        128
static bool  i2c_write_interface(u8* pbt_buf, int dw_lenth);


static u8 CTPM_FW[]=
{
#include "ft_app.i"
};

typedef enum
{
    ERR_OK,
    ERR_MODE,
    ERR_READID,
    ERR_ERASE,
    ERR_STATUS,
    ERR_ECC,
    ERR_DL_ERASE_FAIL,
    ERR_DL_PROGRAM_FAIL,
    ERR_DL_VERIFY_FAIL
}E_UPGRADE_ERR_TYPE;

/***********************************************************************/

/***********************************************************************
    [function]: 
		           callback:                send data to ctpm by i2c interface;
    [parameters]:
			    txdata[in]:              data buffer which is used to send data;
			    length[in]:              the length of the data buffer;
    [return]:
			    FTS_TRUE:              success;
			    FTS_FALSE:             fail;
************************************************************************/
static int fts_i2c_txdata(u8 *txdata, int length)
{
	int ret;

	struct i2c_msg msg;

      msg.addr = this_client->addr;
      msg.flags = 0;
      msg.len = length;
      msg.buf = txdata;
	ret = i2c_transfer(this_client->adapter, &msg, 1);
	if (ret < 0)
		pr_err("%s i2c write error: %d\n", __func__, ret);

	return ret;
}

/***********************************************************************
    [function]: 
		           callback:               write data to ctpm by i2c interface;
    [parameters]:
			    buffer[in]:             data buffer;
			    length[in]:            the length of the data buffer;
    [return]:
			    FTS_TRUE:            success;
			    FTS_FALSE:           fail;
************************************************************************/
static bool  i2c_write_interface(u8* pbt_buf, int dw_lenth)
{
    int ret;
    ret=i2c_master_send(this_client, pbt_buf, dw_lenth);
    if(ret<=0)
    {
        printk("[TSP]i2c_write_interface error line = %d, ret = %d\n", __LINE__, ret);
        return false;
    }

    return true;
}

/***********************************************************************
    [function]: 
		           callback:                read register value ftom ctpm by i2c interface;
    [parameters]:
                        reg_name[in]:         the register which you want to write;
			    tx_buf[in]:              buffer which is contained of the writing value;
    [return]:
			    FTS_TRUE:              success;
			    FTS_FALSE:             fail;
************************************************************************/
static bool fts_register_write(u8 reg_name, u8* tx_buf)
{
	u8 write_cmd[2] = {0};

	write_cmd[0] = reg_name;
	write_cmd[1] = *tx_buf;

	/*call the write callback function*/
	return i2c_write_interface(write_cmd, 2);
}

/***********************************************************************
[function]: 
                      callback:         send a command to ctpm.
[parameters]:
			  btcmd[in]:       command code;
			  btPara1[in]:     parameter 1;    
			  btPara2[in]:     parameter 2;    
			  btPara3[in]:     parameter 3;    
                      num[in]:         the valid input parameter numbers, 
                                           if only command code needed and no 
                                           parameters followed,then the num is 1;    
[return]:
			  FTS_TRUE:      success;
			  FTS_FALSE:     io fail;
************************************************************************/
static bool cmd_write(u8 btcmd,u8 btPara1,u8 btPara2,u8 btPara3,u8 num)
{
    u8 write_cmd[4] = {0};

    write_cmd[0] = btcmd;
    write_cmd[1] = btPara1;
    write_cmd[2] = btPara2;
    write_cmd[3] = btPara3;
    return i2c_write_interface(write_cmd, num);
}

/***********************************************************************
    [function]: 
		           callback:              read data from ctpm by i2c interface;
    [parameters]:
			    buffer[in]:            data buffer;
			    length[in]:           the length of the data buffer;
    [return]:
			    FTS_TRUE:            success;
			    FTS_FALSE:           fail;
************************************************************************/
static bool i2c_read_interface(u8* pbt_buf, int dw_lenth)
{
    int ret;
    
    ret=i2c_master_recv(this_client, pbt_buf, dw_lenth);

    if(ret<=0)
    {
        printk("[TSP]i2c_read_interface error\n");
        return false;
    }
  
    return true;
}


/***********************************************************************
[function]: 
                      callback:         read a byte data  from ctpm;
[parameters]:
			  buffer[in]:       read buffer;
			  length[in]:      the size of read data;    
[return]:
			  FTS_TRUE:      success;
			  FTS_FALSE:     io fail;
************************************************************************/
static bool byte_read(u8* buffer, int length)
{
    return i2c_read_interface(buffer, length);
}

/***********************************************************************
[function]: 
                      callback:         write a byte data  to ctpm;
[parameters]:
			  buffer[in]:       write buffer;
			  length[in]:      the size of write data;    
[return]:
			  FTS_TRUE:      success;
			  FTS_FALSE:     io fail;
************************************************************************/
static bool byte_write(u8* buffer, int length)
{
    
    return i2c_write_interface(buffer, length);
}

/***********************************************************************
    [function]: 
		           callback:                 read register value ftom ctpm by i2c interface;
    [parameters]:
                        reg_name[in]:         the register which you want to read;
			    rx_buf[in]:              data buffer which is used to store register value;
			    rx_length[in]:          the length of the data buffer;
    [return]:
			    FTS_TRUE:              success;
			    FTS_FALSE:             fail;
************************************************************************/
static bool fts_register_read(u8 reg_name, u8* rx_buf, int rx_length)
{
	u8 read_cmd[2]= {0};
	u8 cmd_len 	= 0;

	read_cmd[0] = reg_name;
	cmd_len = 1;	

	/*send register addr*/
	if(!i2c_write_interface(&read_cmd[0], cmd_len))
	{
		return false;
	}

	/*call the read callback function to get the register value*/		
	if(!i2c_read_interface(rx_buf, rx_length))
	{
		return false;
	}
	return true;
}



/***********************************************************************
[function]: 
                        callback:          burn the FW to ctpm.
[parameters]:
			    pbt_buf[in]:     point to Head+FW ;
			    dw_lenth[in]:   the length of the FW + 6(the Head length);    
[return]:
			    ERR_OK:          no error;
			    ERR_MODE:      fail to switch to UPDATE mode;
			    ERR_READID:   read id fail;
			    ERR_ERASE:     erase chip fail;
			    ERR_STATUS:   status error;
			    ERR_ECC:        ecc error.
************************************************************************/
E_UPGRADE_ERR_TYPE  fts_ctpm_fw_upgrade(u8* pbt_buf, int dw_lenth)
{
    u8  cmd,reg_val[2] = {0};
    u8  packet_buf[FTS_PACKET_LENGTH + 6];
    u8  auc_i2c_write_buf[10];
    u8  bt_ecc;
	
    int  j,temp,lenght,i_ret,packet_number, i = 0;
    int  i_is_new_protocol = 0;
	

    /******write 0xaa to register 0xfc******/
    cmd=0xaa;
    fts_register_write(0xfc,&cmd);
    mdelay(50);
	
     /******write 0x55 to register 0xfc******/
    cmd=0x55;
    fts_register_write(0xfc,&cmd);
    printk("[TSP] Step 1: Reset CTPM test\n");
   
    mdelay(10);   


    /*******Step 2:Enter upgrade mode ****/
    printk("\n[TSP] Step 2:enter new update mode\n");
    auc_i2c_write_buf[0] = 0x55;
    auc_i2c_write_buf[1] = 0xaa;
    do
    {
        i ++;
        i_ret = fts_i2c_txdata(auc_i2c_write_buf, 2);
        mdelay(5);
    }while(i_ret <= 0 && i < 10 );

    if (i > 1)
    {
        i_is_new_protocol = 1;
    }

    /********Step 3:check READ-ID********/        
    cmd_write(0x90,0x00,0x00,0x00,4);
    byte_read(reg_val,2);
    if (reg_val[0] == 0x79 && reg_val[1] == 0x3)
    {
        printk("[TSP] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
    }
    else
    {
        return ERR_READID;
        //i_is_new_protocol = 1;
    }
    

     /*********Step 4:erase app**********/
    if (i_is_new_protocol)
    {
        cmd_write(0x61,0x00,0x00,0x00,1);
    }
    else
    {
        cmd_write(0x60,0x00,0x00,0x00,1);
    }
    mdelay(1500);
    printk("[TSP] Step 4: erase. \n");



    /*Step 5:write firmware(FW) to ctpm flash*/
    bt_ecc = 0;
    printk("[TSP] Step 5: start upgrade. \n");
    dw_lenth = dw_lenth - 8;
    packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
    packet_buf[0] = 0xbf;
    packet_buf[1] = 0x00;
    for (j=0;j<packet_number;j++)
    {
        temp = j * FTS_PACKET_LENGTH;
        packet_buf[2] = (u8)(temp>>8);
        packet_buf[3] = (u8)temp;
        lenght = FTS_PACKET_LENGTH;
        packet_buf[4] = (u8)(lenght>>8);
        packet_buf[5] = (u8)lenght;

        for (i=0;i<FTS_PACKET_LENGTH;i++)
        {
            packet_buf[6+i] = pbt_buf[j*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }
        
        byte_write(&packet_buf[0],FTS_PACKET_LENGTH + 6);
        mdelay(FTS_PACKET_LENGTH/6 + 1);
        if ((j * FTS_PACKET_LENGTH % 1024) == 0)
        {
              printk("[TSP] upgrade the 0x%x th byte.\n", ((unsigned int)j) * FTS_PACKET_LENGTH);
        }
    }

    if ((dw_lenth) % FTS_PACKET_LENGTH > 0)
    {
        temp = packet_number * FTS_PACKET_LENGTH;
        packet_buf[2] = (u8)(temp>>8);
        packet_buf[3] = (u8)temp;

        temp = (dw_lenth) % FTS_PACKET_LENGTH;
        packet_buf[4] = (u8)(temp>>8);
        packet_buf[5] = (u8)temp;

        for (i=0;i<temp;i++)
        {
            packet_buf[6+i] = pbt_buf[ packet_number*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }

        byte_write(&packet_buf[0],temp+6);    
        mdelay(20);
    }

    /***********send the last six byte**********/
    for (i = 0; i<6; i++)
    {
        temp = 0x6ffa + i;
        packet_buf[2] = (u8)(temp>>8);
        packet_buf[3] = (u8)temp;
        temp =1;
        packet_buf[4] = (u8)(temp>>8);
        packet_buf[5] = (u8)temp;
        packet_buf[6] = pbt_buf[ dw_lenth + i]; 
        bt_ecc ^= packet_buf[6];

        byte_write(&packet_buf[0],7);  
        mdelay(20);
    }

    /********send the opration head************/
    cmd_write(0xcc,0x00,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("[TSP] Step 6:  ecc read 0x%x, new firmware 0x%x. \n", reg_val[0], bt_ecc);
    if(reg_val[0] != bt_ecc)
    {
        return ERR_ECC;
    }

    /*******Step 7: reset the new FW**********/
    cmd_write(0x07,0x00,0x00,0x00,1);

    return ERR_OK;
}


/***********************************************************************/

int fts_ctpm_fw_upgrade_with_i_file(void)
{
   u8*     pbt_buf = 0;
   int i_ret;
    
   pbt_buf = CTPM_FW;
   i_ret =  fts_ctpm_fw_upgrade(pbt_buf,sizeof(CTPM_FW));
   
   return i_ret;
}

/***********************************************************************/

unsigned char fts_ctpm_get_upg_ver(void)
{
    unsigned int ui_sz;
	
    ui_sz = sizeof(CTPM_FW);
    if (ui_sz > 2)
    {
        return CTPM_FW[ui_sz - 2];
    }
    else
        return 0xff; 
 
}


#define    FT5X0X_REG_THRES  0x80              /* Thresshold, the threshold be low, the sensitivy will be high */
#define    FT5X0X_REG_REPORT_RATE 0x88  /* **************report rate, in unit of 10Hz **************/
#define    FT5X0X_REG_PMODE    0xA5          /* Power Consume Mode 0 -- active, 1 -- monitor, 3 -- sleep */    
#define    FT5X0X_REG_FIRMID   0xA6         /* ***************firmware version **********************/
#define    FT5X0X_REG_NOISE_MODE 0xb2    /* to enable or disable power noise, 1 -- enable, 0 -- disable */


static int  ft5406_probe(struct i2c_client *client ,const struct i2c_device_id *id)
{
	struct ft5x0x_ts_data *ft5x0x_ts;
	struct input_dev *input_dev;
	struct ft5406_platform_data *pdata = pdata = client->dev.platform_data;

	int err = 0;
	int ret = 0;
	int retry = 0;
	u8 buf_w[1];
	u8 buf_r[1];
	const u8 buf_test[1] = {0};
	
    unsigned char reg_value;
    unsigned char reg_version;

	//printk("==ft5x0x_ts_probe=\n");
	
	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}

	if (pdata->init_platform_hw)                              
		pdata->init_platform_hw();

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;
	
	
	ft5x0x_ts = kzalloc(sizeof(*ft5x0x_ts), GFP_KERNEL);
	if (!ft5x0x_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}
/*
	ft5x0x_client.adapter = bus;
	ft5x0x_client.addr= address;
	ft5x0x_client.mode = NORMALMODE; //NORMALNOSTOPMODE;// DIRECTMODE;
	ft5x0x_client.Channel = I2C_CH0;
	ft5x0x_client.speed = 300;
	ft5x0x_client.addressBit=I2C_7BIT_ADDRESS_8BIT_REG;
	ft5x0x_ts->client=&ft5x0x_client; 

	i2c_set_clientdata(&ft5x0x_client, ft5x0x_ts);

	err = i2c_attach_client(&ft5x0x_client);
	if (err < 0)
	{
		printk("ft5x0x attach client failed!!!!\n");
		goto exit_alloc_data_failed;
	}

	INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);
	ft5x0x_ts->ts_workqueue = create_singlethread_workqueue("ft5x0x_ts");
	if (!ft5x0x_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

	rockchip_mux_api_set(TOUCH_INT_IOMUX_PINNAME,TOUCH_INT_IOMUX_PINDIR);
	GPIOSetPinDirection(TOUCH_INT_IOPIN, GPIO_IN);
	GPIOPullUpDown(TOUCH_INT_IOPIN, GPIOPullUp);
	err = request_gpio_irq(TOUCH_INT_IOPIN, ft5x0x_ts_interrupt, GPIOEdgelFalling, ft5x0x_ts);
	if(err < 0)
	{
		printk("ft5x0x_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}
*/
	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		printk("failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}

       this_client = client;

	while(retry < 5)
	{
		ret=ft5406_set_regs(this_client,FT5X0X_REG_PMODE, buf_test,1);
		if(ret > 0)break;
		retry++;
	}
	if(ret <= 0)
	{
		printk("FT5406 I2C TEST ERROR!\n");
		goto exit_err_i2c_test;
	}
	ft5x0x_ts->client = client;
	ft5x0x_ts->irq = client->irq;
	ft5x0x_ts->input_dev = input_dev;

  #if   CONFIG_FT5X0X_MULTITOUCH                //#ifdef
	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
	
	//input_dev->evbit[0] = BIT_MASK(EV_ABS)|BIT_MASK(EV_KEY)|BIT_MASK(EV_SYN);                  //harry 03.21
	     #ifdef  CONFIG_TOUCH_PANEL_KEY
	     set_bit(KEY_HOME, input_dev->keybit);
            set_bit(KEY_MENU, input_dev->keybit);
            set_bit(KEY_BACK, input_dev->keybit);
            #endif

	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 10, 0, 0);
#else
	set_bit(ABS_X, input_dev->absbit);
	set_bit(ABS_Y, input_dev->absbit);
	set_bit(ABS_PRESSURE, input_dev->absbit);
	set_bit(BTN_TOUCH, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev,
	 ABS_PRESSURE, 0, PRESS_MAX, 0 , 0);
#endif


	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	
	input_dev->name		= FT5X0X_NAME;		//dev_name(&client->dev)
	err = input_register_device(input_dev);
	if (err) {
	//	printk("ft5x0x_ts_probe: failed to register input device: \n");
		goto exit_input_register_device_failed;
	}

//	printk("==probe over =\n");

	ft5x0x_ts->irq = RK29_PIN0_PA2;
	if (!ft5x0x_ts->irq) {
		dev_dbg(&ft5x0x_ts->client->dev, "no IRQ?\n");
		return -ENODEV;
	}else{
		ft5x0x_ts->irq = gpio_to_irq(ft5x0x_ts->irq);
	}

//printk("ft5x0x_ts irq  =%d", ft5x0x_ts->irq);


	INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);
	ft5x0x_ts->ts_workqueue = create_singlethread_workqueue("ft5x0x_ts");
	if (!ft5x0x_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

	/***wait CTP to bootup normally***/
	 msleep(200); 
	 
	 fts_register_read(FT5X0X_REG_FIRMID, &reg_version,1);
	 printk("[TSP] firmware version = 0x%2x\n", reg_version);
	 fts_register_read(FT5X0X_REG_REPORT_RATE, &reg_value,1);
	 printk("[TSP]firmware report rate = %dHz\n", reg_value*10);
	 fts_register_read(FT5X0X_REG_THRES, &reg_value,1);
	 printk("[TSP]firmware threshold = %d\n", reg_value * 4);
	 fts_register_read(FT5X0X_REG_NOISE_MODE, &reg_value,1);
	 printk("[TSP]nosie mode = 0x%2x\n", reg_value);

#if 1
	  if (fts_ctpm_get_upg_ver() != reg_version)  
	  {
		  printk("[TSP] start upgrade new verison 0x%2x\n", fts_ctpm_get_upg_ver());
		  msleep(200);
		  err =  fts_ctpm_fw_upgrade_with_i_file();
		  if (err == 0)
		  {
			  printk("[TSP] ugrade successfuly.\n");
			  msleep(300);
			  fts_register_read(FT5X0X_REG_FIRMID, &reg_value,1);
			  printk("FTS_DBG from old version 0x%2x to new version = 0x%2x\n", reg_version, reg_value);
		  }
		  else
		  {
			  printk("[TSP]  ugrade fail err=%d, line = %d.\n",
				  err, __LINE__);
		  }
		  msleep(4000);
	  }
#endif



	//printk("client->dev.driver->name %s  ,%d \n",client->dev.driver->name,ft5x0x_ts->irq);

	ret = request_irq(ft5x0x_ts->irq, ft5x0x_ts_interrupt, /*IRQF_TRIGGER_LOW*/IRQF_TRIGGER_FALLING,     //IRQF_DISABLED|IRQF_TRIGGER_FALLING,
			client->dev.driver->name, ft5x0x_ts);
	
	if (ret < 0) {
		dev_err(&client->dev, "irq %d busy?\n", ft5x0x_ts->irq);
		goto fail3;
	}
                                                                                     //harry 03.23
	#if 0
	ret = input_register_device(input_dev);	
	if(ret<0)
	{
	//	printk("ft5406 register input device failed!!!!\n");
		goto exit_irq_request_failed;
	}
	#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
    ft5406_early_suspend.suspend =ft5x0x_ts_suspend;
   ft5406_early_suspend.resume =ft5x0x_ts_resume;
   ft5406_early_suspend.level = 0x2;
    register_early_suspend(&ft5406_early_suspend);
#endif

	//buf_w[0] = 6;                                   //harry 04.07
	buf_w[0] = 6;
	
	err = ft5406_set_regs(client,0x88,buf_w,1);
		//ft5x0x_i2c_txdata(0x88, buf_w, 1);    /* adjust frequency 60Hz */

	buf_r[0] = 0;
	err = ft5406_read_regs(client,0x88,buf_r,1);
		//ft5x0x_i2c_rxdata(0x88, buf_r, 1);
//	printk("read buf[0x88] = %d\n", buf_r[0]);

    return 0;
fail3:
	free_irq(ft5x0x_ts->irq,ft5x0x_ts);
	i2c_set_clientdata(client, NULL);             //harry
	destroy_workqueue(ft5x0x_ts->ts_workqueue);   //harry
exit_input_register_device_failed:
	input_free_device(input_dev);
//	i2c_set_clientdata(client, NULL);
//	kfree(ft5x0x_ts);

exit_input_dev_alloc_failed:
//	free_irq(7, ft5x0x_ts);
	free_irq(ft5x0x_ts->irq, ft5x0x_ts);
exit_irq_request_failed:
exit_platform_data_null:
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
exit_create_singlethread:
exit_err_i2c_test:
	printk("==singlethread error =\n");
	kfree(ft5x0x_ts);
exit_alloc_data_failed:
	return err;
}



static struct i2c_device_id ft5406_idtable[] = {
	{ FT5X0X_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, ft5406_idtable);

static struct i2c_driver ft5406_driver  = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= FT5X0X_NAME
	},
	.id_table	= ft5406_idtable,
	.probe = ft5406_probe,
#ifndef CONFIG_HAS_EARLYSUSPEND		
       .suspend	= ft5x0x_ts_suspend,
	.resume	= ft5x0x_ts_resume,
#endif	
	.remove 	= __devexit_p(ft5406_remove),
};

static int __init ft5x0x_ts_init(void)
{
	return i2c_add_driver(&ft5406_driver);
}

static void __exit ft5x0x_ts_exit(void)
{
	i2c_del_driver(&ft5406_driver);
}


/***********************************************************************/

module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");

