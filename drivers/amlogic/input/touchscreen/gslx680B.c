/*
 * drivers/input/touchscreen/gslX680.c
 *
 * Copyright (c) 2012 Shanghai Basewin
 *	Guan Yuwei<guanyuwei@basewin.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */



#include "linux/amlogic/input/common.h"
#include <linux/amlogic/input/gslx680B.h>
#include "gslx680B-fw.h"

/*#include <linux/syscore_ops.h>
#include <mach/am_regs.h>
#include <mach/gpio.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <linux/err.h>
#include <linux/gpio.h>
*/
//#define GSL_DEBUG
#define REPORT_DATA_ANDROID_4_0

//#define HAVE_TOUCH_KEY

//#define LATE_UPGRADE

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

#define PRESS_MAX    		255
#define MAX_CONTACTS 		10
#define DMA_TRANS_LEN		0x20


//#undef GSL_NOID_VERSION
//#include "gxl.h"

#ifdef HAVE_TOUCH_KEY
static u16 key = 0;
static int key_state_flag = 0;
struct key_data {
	u16 key;
	u16 x_min;
	u16 x_max;
	u16 y_min;
	u16 y_max;	
};

const u16 key_array[]={
                                      KEY_BACK,
                                      KEY_HOME,
                                      KEY_MENU,
                                      KEY_SEARCH,
                                     }; 
#define MAX_KEY_NUM     (sizeof(key_array)/sizeof(key_array[0]))

struct key_data gsl_key_data[MAX_KEY_NUM] = {
	{KEY_BACK, 2048, 2048, 2048, 2048},
	{KEY_HOME, 2048, 2048, 2048, 2048},	
	{KEY_MENU, 2048, 2048, 2048, 2048},
	{KEY_SEARCH, 2048, 2048, 2048, 2048},
};
#endif

struct gsl_ts_data {
	u8 x_index;
	u8 y_index;
	u8 z_index;
	u8 id_index;
	u8 touch_index;
	u8 data_reg;
	u8 status_reg;
	u8 data_size;
	u8 touch_bytes;
	u8 update_data;
	u8 touch_meta_data;
	u8 finger_size;
};

static struct gsl_ts_data devices[] = {
	{
		.x_index = 6,
		.y_index = 4,
		.z_index = 5,
		.id_index = 7,
		.data_reg = GSL_DATA_REG,
		.status_reg = GSL_STATUS_REG,
		.update_data = 0x4,
		.touch_bytes = 4,
		.touch_meta_data = 4,
		.finger_size = 70,
	},
};

struct gsl_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *wq;
	struct gsl_ts_data *dd;
	u8 *touch_data;
	u8 device_id;
	u8 prev_touches;
	bool is_suspended;
	bool int_pending;
	struct mutex sus_lock;
//	uint32_t gpio_irq;
	int irq;
#if defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
	struct work_struct dl_work;
	struct semaphore fw_sema;
	int dl_fw;
	struct touch_pdata *g_pdata ;
};


static struct gsl_ts *Gts =NULL;

#ifdef GSL_DEBUG 
#define print_info(fmt, args...)   \
        do{                              \
                printk(fmt, ##args);     \
        }while(0)
#else
#define print_info touch_dbg
#endif

static u32 id_sign[MAX_CONTACTS+1] = {0};
static u8 id_state_flag[MAX_CONTACTS+1] = {0};
static u8 id_state_old_flag[MAX_CONTACTS+1] = {0};
static u16 x_old[MAX_CONTACTS+1] = {0};
static u16 y_old[MAX_CONTACTS+1] = {0};
static u16 x_new = 0;
static u16 y_new = 0;
static int count, x_temp, y_temp;

static int SCREEN_MAX_X;
static int SCREEN_MAX_Y;
static int MAX_FINGERS;
static struct touch_pdata *g_pdata = NULL;
extern struct touch_pdata *ts_com;
static struct i2c_client *this_client;
#include <mach/gpio.h>
#include <plat/platform.h>
#include <plat/platform_data.h>

static int gslX680_shutdown_low(struct touch_pdata *pdata)
{
#if 0//def CONFIG_OF
  if (pdata->gpio_reset) {
    set_reset_pin(pdata, 0);
  }
#else
	//gpio_interrupt = "GPIOZ_7";
		//	gpio_reset = "GPIOZ_6";

 printk("GPIOZ_6 %d#########\n", GPIOZ_6);//hycui
  //gpio_set_status(GPIOZ_6, gpio_status_out);
  //gpio_out(GPIOZ_6, 0);
  aml_gpio_direction_output(GPIOZ_6, 0);
  	//	aml_gpio_direction_input(g_pdata->gpio_interrupt);

#endif 
	return 0;
}

static int gslX680_shutdown_high(struct touch_pdata *pdata)
{

#if 0 //hycui def CONFIG_OF
  if (pdata->gpio_reset) {
    set_reset_pin(pdata, 1); 
  }
#else
	//gpio_set_status(PAD_GPIOC_3, gpio_status_out);
 // gpio_out(PAD_GPIOC_3, 1);
    aml_gpio_direction_output(GPIOZ_6, 1);

#endif
	return 0;
}

static void gslx680_hardware_reset(struct touch_pdata *pdata)
{
	gslX680_shutdown_low(pdata);
	mdelay(10);  // > 10us
	gslX680_shutdown_high(pdata);
	mdelay(120); // > 100ms
}

static void gslx680_software_reset(struct touch_pdata *pdata)
{
	return;
}

static int gslX680_chip_init(struct touch_pdata *pdata)
{	
	gslX680_shutdown_low(pdata);
	mdelay(10);  // > 10us
	
	gslX680_shutdown_high(pdata);
	mdelay(120); // > 100ms
	
#ifdef CONFIG_OF
	/* if (pdata->gpio_interrupt) {
       aml_gpio_direction_input(pdata->gpio_interrupt);
	   aml_gpio_to_irq(pdata->gpio_interrupt, pdata->irq-INT_GPIO_0, pdata->irq_edge);
	} 
	*/
#else
  gpio_set_status(PAD_GPIOA_16, gpio_status_in);
  gpio_irq_set(PAD_GPIOA_16, GPIO_IRQ(INT_GPIO_0-INT_GPIO_0, GPIO_IRQ_RISING));
	msleep(20);
#endif
	return 0;
}
static inline u16 join_bytes(u8 a, u8 b)
{
	u16 ab = 0;
	ab = ab | a;
	ab = ab << 8 | b;
	return ab;
}

static u32 gsl_read_interface(struct i2c_client *client, u8 reg, u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[2];

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = &reg;

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags |= I2C_M_RD;
	xfer_msg[1].buf = buf;

	if (reg < 0x80) {
		i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg));
		msleep(5);
	}

	return i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg)) == ARRAY_SIZE(xfer_msg) ? 0 : -EFAULT;
}

static u32 gsl_write_interface(struct i2c_client *client, const u8 reg, u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[1];

	buf[0] = reg;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static __inline__ void fw2buf(u8 *buf, const u32 *fw)
{
	u32 *u32_buf = (int *)buf;
	*u32_buf = *fw;
}

#define READ_COUNT  18
struct fw_datB
{
    u8 offset;
    u32 val;
}fw_datB;
static void gsl_load_fw(struct i2c_client *client)
{
	u32 offset = 0;
	int file_size;
	struct fw_datB *fw_d = NULL;
	u8 send_flag = 0;
	u8 buf[DMA_TRANS_LEN*4 + 1] = {0};
	u8 *cur = buf + 1;
	u8 tmp[READ_COUNT] = {0};
	int ret = 0;
	file_size = touch_open_fw(g_pdata->fw_file);
  if(file_size < 0) {
		printk("%s: no fw file\n", ts_com->owner);
		return;
	}	
	if (fw_d == NULL)
		fw_d = kzalloc(sizeof(*fw_d), GFP_KERNEL);
	if (fw_d == NULL) {
		printk("Insufficient memory in upgrade!\n");
		return ;
	}

  //printk("file_size = %d\n",file_size);
	printk("=============gsl_load_fw start==============\n");
  while (offset < file_size) {
		while (send_flag < DMA_TRANS_LEN+1) {
			if (offset >= file_size) break;
			touch_read_fw(offset, READ_COUNT, &tmp[0]);
			ret = sscanf(&tmp[0],"{0x%x,0x%lx},",&(fw_d->offset),&(fw_d->val));
			if (ret != 2) {
				offset ++;
				continue;
			}
		  if (fw_d->offset == GSL_PAGE_REG) {
				fw2buf(cur, &(fw_d->val));
				gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
				send_flag ++;
		  }
		  else {
				if (send_flag == 1) {
					buf[0] = (u8)fw_d->offset;
				}
				fw2buf(cur, &(fw_d->val));
				cur += 4;
				send_flag ++;
			}
		  offset ++;
		 }
		if (offset >= file_size) break;
		gsl_write_interface(client, buf[0], buf, cur - buf - 1);
		cur = buf + 1;
		send_flag = 0;
	}
	touch_close_fw();
	if (fw_d != NULL) {
     kfree(fw_d);
     fw_d = NULL;
  }
	printk("=============gsl_load_fw end==============\n");
}

static int gsl_ts_write(struct i2c_client *client, u8 addr, u8 *pdata, int datalen)
{
	int ret = 0;
	u8 tmp_buf[128];
	unsigned int bytelen = 0;
	if (datalen > 125)
	{
		printk("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}
	
	tmp_buf[0] = addr;
	bytelen++;
	
	if (datalen != 0 && pdata != NULL)
	{
		memcpy(&tmp_buf[bytelen], pdata, datalen);
		bytelen += datalen;
	}
	
	ret = i2c_master_send(client, tmp_buf, bytelen);
	return ret;
}

static int gsl_ts_read(struct i2c_client *client, u8 addr, u8 *pdata, unsigned int datalen)
{
	int ret = 0;

	if (datalen > 126)
	{
		printk("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}

	ret = gsl_ts_write(client, addr, NULL, 0);
	if (ret < 0)
	{
		printk("%s set data address fail!\n", __func__);
		return ret;
	}
	
	return i2c_master_recv(client, pdata, datalen);
}

static int test_i2c(struct i2c_client *client)
{
	u8 read_buf = 0;
	u8 write_buf = 0x12;
	u8 retry;
	int ret;
	ret = gsl_ts_read( client, 0xf0, &read_buf, sizeof(read_buf) );
	if  (ret  < 0)  
	{
		print_info("I2C transfer error!\n");
	}
	else
	{
		print_info("I read reg 0xf0 is %x\n", read_buf);
	}
	msleep(10);

	for (retry=0; retry<3; retry++) {
	ret = gsl_ts_write(client, 0xf0, &write_buf, sizeof(write_buf));
	if  (ret  < 0)  
	{
		print_info("I2C transfer error!\n");
	}
	else
	{
		print_info("I write reg 0xf0 0x12\n");
	}
	msleep(10);

	ret = gsl_ts_read( client, 0xf0, &read_buf, sizeof(read_buf) );
	if  (ret  <  0 )
	{
		print_info("I2C transfer error!\n");
	}
	else
	{
		print_info("I read reg 0xf0 is 0x%x\n", read_buf);
	}
	msleep(10);

			if (read_buf == write_buf)
				 return 0;
	}

	return -1;

}


static void startup_chip(struct i2c_client *client)
{

	u8 tmp = 0x00;
	
  #ifdef GSL_NOID_VERSION
	gsl_DataInit(gsl_config_data_idB);
#endif

	print_info("%s start\n", __func__);
	gsl_ts_write(client, 0xe0, &tmp, 1);
	msleep(10);
	print_info("%s end\n", __func__);
}

static void reset_chip(struct i2c_client *client)
{
	u8 buf[4] = {0x00};
	u8 tmp = 0x88;
//	printk("%s start ################\n", __func__);
	gsl_ts_write(client, 0xe0, &tmp, sizeof(tmp));
	msleep(10);

	tmp = 0x04;
	gsl_ts_write(client, 0xe4, &tmp, sizeof(tmp));
	msleep(10);

	gsl_ts_write(client, 0xbc, buf, sizeof(buf));
	msleep(10);
	print_info("%s end\n", __func__);
}

static void clr_reg(struct i2c_client *client)
{
	u8 write_buf[4]	= {0};

	write_buf[0] = 0x88;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1); 	
	msleep(20);
	write_buf[0] = 0x03;
	gsl_ts_write(client, 0x80, &write_buf[0], 1); 	
	msleep(5);
	write_buf[0] = 0x04;
	gsl_ts_write(client, 0xe4, &write_buf[0], 1); 	
	msleep(5);
	write_buf[0] = 0x00;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1); 	
	msleep(20);
}

static void gsl_load_fw1(struct i2c_client *client);

static void init_chip(struct i2c_client *client)
{
	disable_irq_nosync(client->irq); //hycui
	//test_i2c(client);
	printk("init_chip ##############\n");//hycui
	gslX680_shutdown_low(g_pdata);
	msleep(20);
	gslX680_shutdown_high(g_pdata);
	msleep(20);
	clr_reg(client);
	reset_chip(client);
	gsl_load_fw1(client);
	startup_chip(client);
	reset_chip(client);
	startup_chip(client); 
	msleep(5);
	enable_irq(client->irq);
}


static void gsl_load_fw1(struct i2c_client *client)
{
	u8 buf[DMA_TRANS_LEN*4 + 1] = {0};
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	u32 source_len = ARRAY_SIZE(GSLX680_FW);

	printk("=============gsl_load_fw1 start==============\n");

	for (source_line = 0; source_line < source_len; source_line++) 
	{
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == GSLX680_FW[source_line].offset)
		{
			fw2buf(cur, &GSLX680_FW[source_line].val);
			gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
			send_flag = 1;
		}
		else 
		{
			if (1 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20))
	    			buf[0] = (u8)GSLX680_FW[source_line].offset;

			fw2buf(cur, &GSLX680_FW[source_line].val);
			cur += 4;

			if (0 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20)) 
			{
	    			gsl_write_interface(client, buf[0], buf, cur - buf - 1);
	    			cur = buf + 1;
			}

			send_flag++;
		}
	}

	printk("=============gsl_load_fw1 end==============\n");

}


static void init_chip1(struct i2c_client *client)
{
	gslX680_shutdown_low(g_pdata);
	msleep(20);
	gslX680_shutdown_high(g_pdata);
	msleep(20);
	clr_reg(client);
    reset_chip(client);
	gsl_load_fw1(client);
	startup_chip(client);
	reset_chip(client);
	startup_chip(client);		
}


static void check_mem_data1(struct i2c_client *client, int force)
{
	u8 read_buf[4]  = {0};
	
	msleep(30);
	gsl_ts_read(client,0xb0, read_buf, sizeof(read_buf));
	
	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a || read_buf[0] != 0x5a || force)
	{
		printk("#########check mem read 0xb0 = %x %x %x %x #########\n", read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		init_chip1(client);
	}
}



static void check_mem_data(struct i2c_client *client)
{
	u8 read_buf[4]  = {0};
	
	msleep(30);
	gsl_ts_read(client,0xb0, read_buf, sizeof(read_buf));
	
	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a || read_buf[0] != 0x5a)
	{
		printk("#########check mem read 0xb0 = %x %x %x %x #########\n", read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		init_chip(client);
	}
}


#ifdef FILTER_POINT
static void filter_point(u16 x, u16 y , u8 id)
{
	u16 x_err =0;
	u16 y_err =0;
	u16 filter_step_x = 0, filter_step_y = 0;
	
	id_sign[id] = id_sign[id] + 1;
	if(id_sign[id] == 1)
	{
		x_old[id] = x;
		y_old[id] = y;
	}
	
	x_err = x > x_old[id] ? (x -x_old[id]) : (x_old[id] - x);
	y_err = y > y_old[id] ? (y -y_old[id]) : (y_old[id] - y);

	if( (x_err > FILTER_MAX && y_err > FILTER_MAX/3) || (x_err > FILTER_MAX/3 && y_err > FILTER_MAX) )
	{
		filter_step_x = x_err;
		filter_step_y = y_err;
	}
	else
	{
		if(x_err > FILTER_MAX)
			filter_step_x = x_err; 
		if(y_err> FILTER_MAX)
			filter_step_y = y_err;
	}

	if(x_err <= 2*FILTER_MAX && y_err <= 2*FILTER_MAX)
	{
		filter_step_x >>= 2; 
		filter_step_y >>= 2;
	}
	else if(x_err <= 3*FILTER_MAX && y_err <= 3*FILTER_MAX)
	{
		filter_step_x >>= 1; 
		filter_step_y >>= 1;
	}	

	x_new = x > x_old[id] ? (x_old[id] + filter_step_x) : (x_old[id] - filter_step_x);
	y_new = y > y_old[id] ? (y_old[id] + filter_step_y) : (y_old[id] - filter_step_y);

	x_old[id] = x_new;
	y_old[id] = y_new;
}
#endif

static void record_point(u16 x, u16 y , u8 id)
{
	u16 x_err =0;
	u16 y_err =0;

	id_sign[id]=id_sign[id]+1;
	
	if(id_sign[id]==1){
		x_old[id]=x;
		y_old[id]=y;
	}

	x = (x_old[id] + x)/2;
	y = (y_old[id] + y)/2;
		
	if(x>x_old[id]){
		x_err=x -x_old[id];
	}
	else{
		x_err=x_old[id]-x;
	}

	if(y>y_old[id]){
		y_err=y -y_old[id];
	}
	else{
		y_err=y_old[id]-y;
	}

	if( (x_err > 3 && y_err > 1) || (x_err > 1 && y_err > 3) ){
		x_new = x;     x_old[id] = x;
		y_new = y;     y_old[id] = y;
	}
	else{
		if(x_err > 3){
			x_new = x;     x_old[id] = x;
		}
		else
			x_new = x_old[id];
		if(y_err> 3){
			y_new = y;     y_old[id] = y;
		}
		else
			y_new = y_old[id];
	}

	if(id_sign[id]==1){
		x_new= x_old[id];
		y_new= y_old[id];
	}
	
}

#ifdef HAVE_TOUCH_KEY
static void report_key(struct gsl_ts *ts, u16 x, u16 y)
{
	u16 i = 0;

	for(i = 0; i < MAX_KEY_NUM; i++) 
	{
		if((gsl_key_data[i].x_min < x) && (x < gsl_key_data[i].x_max)&&(gsl_key_data[i].y_min < y) && (y < gsl_key_data[i].y_max))
		{
			key = gsl_key_data[i].key;	
			input_report_key(ts->input, key, 1);
			input_sync(ts->input); 		
			key_state_flag = 1;
			break;
		}
	}
}
#endif

static void report_data(struct gsl_ts *ts, u16 x, u16 y, u8 pressure, u8 id)
{

	//printk("#####id=%d,x=%d, y=%d  %d %d######\n",id,x,y, (SCREEN_MAX_X-x),SCREEN_MAX_Y );;
	if(x>=SCREEN_MAX_X||y>=SCREEN_MAX_Y)
	{
	#ifdef HAVE_TOUCH_KEY
		report_key(ts,x,y);
	#endif
		return;
	}
	
#ifdef REPORT_DATA_ANDROID_4_0
	input_mt_slot(ts->input, id);		
	 int x1 = (SCREEN_MAX_X-x);
	 if (x1 < 0) x1 =0;
	// int y1 = (SCREEN_MAX_Y-y);
	// if (y1 < 0) y1 =0;
	// d
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X, y);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, x1);	
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
#else
	//input_report_key(ts->input, BTN_TOUCH, 1);
	 int x1 = (SCREEN_MAX_X-y);
	 if (x1 < 0) x1 =0;
	 int y1 = (SCREEN_MAX_Y-x); 
	 if (y1 < 0) y1 =0;
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X,y1);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, x1));
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
	input_mt_sync(ts->input);
#endif
}

#define X_SUB 100
#define Y_SUB 50

static void process_gslX680_data(struct gsl_ts *ts)
{
	u8 id, touches;
	u16 x, y;
	int i = 0;

#ifdef GSL_NOID_VERSION
	u32 tmp1;
	u8 buf[4] = {0};
	struct gsl_touch_info cinfo = {0};
#endif
	touches = ts->touch_data[ts->dd->touch_index];
#ifdef GSL_NOID_VERSION
	cinfo.finger_num = touches;
	print_info("tp-gsl  finger_num = %d\n",cinfo.finger_num);
	for(i = 0; i < (touches < MAX_CONTACTS ? touches : MAX_CONTACTS); i ++)
	{
		cinfo.x[i] = join_bytes( ( ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf),
				ts->touch_data[ts->dd->x_index + 4 * i]);
		cinfo.y[i] = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
				ts->touch_data[ts->dd->y_index + 4 * i ]);
		print_info("tp-gsl  x = %d y = %d \n",cinfo.x[i],cinfo.y[i]);
	}
	cinfo.finger_num=(ts->touch_data[3]<<24)|(ts->touch_data[2]<<16)
		|(ts->touch_data[1]<<8)|(ts->touch_data[0]);
	gsl_alg_id_main(&cinfo);
	tmp1=gsl_mask_tiaoping();
	print_info("[tp-gsl] tmp1=%x\n",tmp1);
	if(tmp1>0&&tmp1<0xffffffff)
	{
		buf[0]=0xa;buf[1]=0;buf[2]=0;buf[3]=0;
		gsl_ts_write(ts->client,0xf0,buf,4);
		buf[0]=(u8)(tmp1 & 0xff);
		buf[1]=(u8)((tmp1>>8) & 0xff);
		buf[2]=(u8)((tmp1>>16) & 0xff);
		buf[3]=(u8)((tmp1>>24) & 0xff);
		print_info("tmp1=%08x,buf[0]=%02x,buf[1]=%02x,buf[2]=%02x,buf[3]=%02x\n",
			tmp1,buf[0],buf[1],buf[2],buf[3]);
		gsl_ts_write(ts->client,0x8,buf,4);
	}
	touches = cinfo.finger_num;
#endif
	for(i=1;i<=MAX_CONTACTS;i++)
	{
		if(touches == 0)
			id_sign[i] = 0;	
		id_state_flag[i] = 0;
	}
	for(i= 0;i < (touches > MAX_FINGERS ? MAX_FINGERS : touches);i ++)
	{
	#ifdef GSL_NOID_VERSION
		id = cinfo.id[i];
		x =  cinfo.x[i];
		y =  cinfo.y[i];	
	#else
		x = join_bytes( ( ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf),
				ts->touch_data[ts->dd->x_index + 4 * i]);
		y = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
				ts->touch_data[ts->dd->y_index + 4 * i ]);
		id = ts->touch_data[ts->dd->id_index + 4 * i] >> 4;
	#endif

		if(1 <=id && id <= MAX_CONTACTS)
		{
    #ifdef FILTER_POINT
      filter_point(x, y , id);
    #else
			record_point(x, y , id);
    #endif
    
    if(g_pdata->pol & 0x4)
			swap(x_new, y_new);
    if(g_pdata->pol & 0x1)
    	x_new = SCREEN_MAX_X - x_new;
    if(g_pdata->pol & 0x2)
    	y_new = SCREEN_MAX_Y - y_new;

			if ((((x_new>0 && x_new<X_SUB) && (y_new>0 && y_new<Y_SUB))
				||((x_new>SCREEN_MAX_X-X_SUB && x_new<SCREEN_MAX_X) && (y_new>0 && y_new<Y_SUB))
				||((x_new>0 && x_new<X_SUB) && (y_new>SCREEN_MAX_Y-Y_SUB && y_new<SCREEN_MAX_Y))
				||((x_new>SCREEN_MAX_X-X_SUB && x_new<SCREEN_MAX_X) && (y_new>SCREEN_MAX_Y-Y_SUB && y_new<SCREEN_MAX_Y)))
				&& (id==1)) {
				//printk("2#####id=%d,x_new=%d,y_new=%d######\n",id,x_new,y_new);
				if (count == 0) {
					x_temp = x_new;
					y_temp = y_new;
					count++;
				}
				x_temp = (x_temp>>1) + (x_new>>1);
				y_temp = (y_temp>>1) + (y_new>>1);
				report_data(ts, x_temp, y_temp, 100, id);
				//printk("2#####id=%d,x_temp=%d,y_temp=%d######\n",id,x_temp,y_temp);
		  }
	      else {
		  		count = 0;
				//printk("1#####id=%d,x_new=%d,y_new=%d######\n",id,x_new,y_new);
				report_data(ts, x_new, y_new, 100, id);
	      }

			id_state_flag[id] = 1;
		}
	}
	for(i=1;i<=MAX_CONTACTS;i++)
	{	
		if( (0 == touches) || ((0 != id_state_old_flag[i]) && (0 == id_state_flag[i])) )
		{
			count = 0;
		#ifdef REPORT_DATA_ANDROID_4_0
			input_mt_slot(ts->input, i);
			input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
		#endif
			id_sign[i]=0;
		}
		id_state_old_flag[i] = id_state_flag[i];
	}
#ifndef REPORT_DATA_ANDROID_4_0
	if(0 == touches)
	{	
		//input_report_key(ts->input, BTN_TOUCH, 0);
		input_mt_sync(ts->input);
	#ifdef HAVE_TOUCH_KEY
		if(key_state_flag)
		{
      input_report_key(ts->input, key, 0);
			input_sync(ts->input);
			key_state_flag = 0;
		}
	#endif			
	}
#endif
	input_sync(ts->input);
	ts->prev_touches = touches;
}


static void gsl_ts_xy_worker(struct work_struct *work)
{
	int rc;
	u8 read_buf[4] = {0};

	struct gsl_ts *ts = container_of(work, struct gsl_ts,work);

	print_info("---gsl_ts_xy_worker---\n");				 

	/* read data from DATA_REG */
	rc = gsl_ts_read(ts->client, 0x80, ts->touch_data, ts->dd->data_size);
	print_info("---touches: %d ---\n",ts->touch_data[0]);		
		
	if (rc < 0) 
	{
		dev_err(&ts->client->dev, "read failed\n");
		goto schedule;
	}

	if (ts->touch_data[ts->dd->touch_index] == 0xff) {
		goto schedule;
	}

	rc = gsl_ts_read( ts->client, 0xbc, read_buf, sizeof(read_buf));
	if (rc < 0) 
	{
		dev_err(&ts->client->dev, "read 0xbc failed\n");
		goto schedule;
	}
	print_info("//////// reg %x : %x %x %x %x\n",0xbc, read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		
	if (read_buf[3] == 0 && read_buf[2] == 0 && read_buf[1] == 0 && read_buf[0] == 0)
	{
		process_gslX680_data(ts);
	}
	else
	{
		reset_chip(ts->client);
		startup_chip(ts->client);
	}
	
schedule:
	enable_irq(ts->irq);
		
}

static irqreturn_t gsl_ts_irq(int irq, void *dev_id)
{	
	struct gsl_ts *ts = dev_id;
	static int irq_count = 0;
	
	//printk("==========GSLX680 Interrupt============\n");
	//print_info("irq count: %d\n", irq_count++);
	disable_irq_nosync(ts->irq);

	if (!work_pending(&ts->work)) 
	{
		queue_work(ts->wq, &ts->work);
	}
	
	return IRQ_HANDLED;

}

static int gsl_ts_init_ts(struct i2c_client *client, struct gsl_ts *ts)
{
	struct input_dev *input_device;
	int i, rc = 0;
	
	printk("[GSLX680] Enter %s\n", __func__);

	
	ts->dd = &devices[ts->device_id];

	if (ts->device_id == 0) {
		ts->dd->data_size = MAX_FINGERS * ts->dd->touch_bytes + ts->dd->touch_meta_data;
		ts->dd->touch_index = 0;
	}

	ts->touch_data = kzalloc(ts->dd->data_size, GFP_KERNEL);
	if (!ts->touch_data) {
		pr_err("%s: Unable to allocate memory\n", __func__);
		return -ENOMEM;
	}

	ts->prev_touches = 0;

	input_device = input_allocate_device();
	if (!input_device) {
		rc = -ENOMEM;
		goto error_alloc_dev;
	}

	ts->input = input_device;
	input_device->name = GSLX680_I2C_NAME;
	input_device->id.bustype = BUS_I2C;
	input_device->dev.parent = &client->dev;
	input_set_drvdata(input_device, ts);

#ifdef REPORT_DATA_ANDROID_4_0
	__set_bit(EV_ABS, input_device->evbit);
	__set_bit(EV_KEY, input_device->evbit);
	__set_bit(EV_REP, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	input_mt_init_slots(input_device, (MAX_CONTACTS+1), 0);
#else
	input_set_abs_params(input_device,ABS_MT_TRACKING_ID, 0, (MAX_CONTACTS+1), 0, 0);
	set_bit(EV_ABS, input_device->evbit);
	set_bit(EV_KEY, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	//input_device->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
#endif

#ifdef HAVE_TOUCH_KEY
	input_device->evbit[0] = BIT_MASK(EV_KEY);
	for (i = 0; i < MAX_KEY_NUM; i++)
		set_bit(key_array[i], input_device->keybit);
#endif

	set_bit(ABS_MT_POSITION_X, input_device->absbit);
	set_bit(ABS_MT_POSITION_Y, input_device->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_device->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_device->absbit);

	input_set_abs_params(input_device,ABS_MT_POSITION_X, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_device,ABS_MT_POSITION_Y, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_device,ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_device,ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);

	ts->irq = client->irq;

	ts->wq = create_singlethread_workqueue("kworkqueue_ts");
	if (!ts->wq) {
		dev_err(&client->dev, "Could not create workqueue\n");
		goto error_wq_create;
	}
	flush_workqueue(ts->wq);	

	INIT_WORK(&ts->work, gsl_ts_xy_worker);

	rc = input_register_device(input_device);
	if (rc)
		goto error_unreg_device;

	return 0;

error_unreg_device:
	destroy_workqueue(ts->wq);
error_wq_create:
	input_free_device(input_device);
error_alloc_dev:
	kfree(ts->touch_data);
	return rc;
}

static void do_download(struct work_struct *work)
{
	struct gsl_ts *ts = container_of(work, struct gsl_ts, dl_work);
	printk("gsl1680 call func start%s\n", __func__);

	gslX680_shutdown_high(g_pdata);
	msleep(20); 	
	reset_chip(ts->client);
	startup_chip(ts->client);	
	check_mem_data(ts->client);
	printk("gsl1680 0000\n");
	up(&ts->fw_sema);
	
	printk("gsl1680 call func end%s\n", __func__);
}

static int gsl_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct gsl_ts *ts = dev_get_drvdata(&(client->dev));
	int rc = 0;

	printk("gsl1680 call func start%s #####################\n", __func__);
 
    disable_irq_nosync(ts->irq); //hycui
	reset_chip(ts->client);
	gslX680_shutdown_low(ts->g_pdata);
	//msleep(15); 		

	return 0; 
}

static int gsl_ts_resume(struct i2c_client *client)
{
	struct gsl_ts *ts = dev_get_drvdata(&(client->dev));
	int rc = 0;

  	printk("gsl1680 call func start%s ###########\n", __func__);
	
	gslX680_shutdown_high(ts->g_pdata);
	reset_chip(ts->client);
	startup_chip(ts->client);	
	check_mem_data1(ts->client, 0);
	enable_irq(ts->irq);
	
  	//ts->dl_fw = 1;
	 
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void gsl_ts_early_suspend(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);
	printk("gsl1680 call func start%s %d#########\n", __func__, ts->irq);

	disable_irq(ts->irq);	
	//reset_chip(ts->client);
	gslX680_shutdown_low(g_pdata);
	msleep(15); 			
}
#if 0
static void gslx680_late_upgrade1(void)
{
   //printk("test #$$$$$$$$$$$$$$$$$ %d %d##\n", Gts->irq, Gts->client->irq);
    disable_irq_nosync(Gts->irq); //hycui
	check_mem_data1(Gts->client, 1);
	enable_irq(Gts->irq);
 //   do_exit(0);
}

void  gsl_TS_GO()
{
  if (Gts )//&& Gts->dl_fw) //hycui
  {
     Gts->dl_fw =0;
   gslX680_shutdown_high(g_pdata);
	 msleep(20); 	
	reset_chip(Gts->client);
	startup_chip(Gts->client);	
		/**check_mem_data1(Gts->client);
	*/
	 //printk("test ##3##################\n");
  //	disable_irq_nosync(Gts->client->irq); //hycui
	//  schedule_work(&(Gts->dl_work));
	 kthread_run(gslx680_late_upgrade1, NULL, "gslx680_late_upgrade1");
	// msleep(35);
	// msleep(10);
  }
}
#endif

static void gsl_ts_late_resume(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);
    gslX680_shutdown_high(g_pdata);
	aml_gpio_direction_output(g_pdata->gpio_interrupt, 0);
	msleep(20); 	
	reset_chip(ts->client);
	startup_chip(ts->client);	
    aml_gpio_direction_input(g_pdata->gpio_interrupt);

	check_mem_data1(ts->client,ts->dl_fw);

	enable_irq(ts->irq);
	ts->dl_fw =0;
	printk("###gsl_ts_late_resume end#################\n");
 
}
#endif

static void gslx680_upgrade_touch(void)
{
		init_chip(this_client);
		check_mem_data(this_client);
}

#ifdef LATE_UPGRADE
static void gslx680_late_upgrade(void)
{
	int file_size;
//static int count;
	while(1) {
		file_size = touch_open_fw(g_pdata->fw_file);
		if(file_size < 0) {
//			printk("%s: %d\n", __func__, count++);
			msleep(10);
		}
		else break;
	}
	touch_close_fw();
	gslx680_upgrade_touch();
	printk("%s first load firmware\n", g_pdata->owner);
	enable_irq(this_client->irq);
	do_exit(0);
}
#endif
static void gslx680_test_i2c(char *ver)
{
	if (test_i2c(this_client))
		printk("gslx680 test fail\n");
	else
		printk("gslx680 test success\n");
}
static int gsl_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct gsl_ts *ts;
	int rc;

	printk("GSLX680 Enter %s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C functionality not supported\n");
		return -ENODEV;
	}

	if (ts_com->owner != NULL) return -ENODEV;
	memset(ts_com, 0 ,sizeof(struct touch_pdata));
	g_pdata = (struct touch_pdata*)client->dev.platform_data;
	ts_com = g_pdata;
	if (request_touch_gpio(g_pdata) != ERR_NO)
		goto exit_check_functionality_failed;
	ts_com->hardware_reset = gslx680_hardware_reset;
	ts_com->software_reset = gslx680_software_reset;
	ts_com->read_version = gslx680_test_i2c;
	ts_com->upgrade_touch = gslx680_upgrade_touch;
	SCREEN_MAX_X = g_pdata->xres;
	SCREEN_MAX_Y = g_pdata->yres;
	MAX_FINGERS = g_pdata->max_num;
	gslX680_chip_init(g_pdata);
	printk("===== gslx680 TP test start =====\n");
	rc = test_i2c(client);
	if(rc){
		printk("!!! gslx680 TP is not exist !!!\n");
		rc = -ENODEV;
		goto exit_check_functionality_failed;
	}
	printk("===== gslx680 TP test ok=======\n");

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	printk("==kzalloc success=\n");

	ts->client = client;
	this_client = client;
	i2c_set_clientdata(client, ts);
	ts->device_id = id->driver_data;

	ts->is_suspended = false;
	ts->int_pending = false;
	
	ts->g_pdata = g_pdata;
	mutex_init(&ts->sus_lock);
	
	//INIT_WORK(&(ts->dl_work), do_download);
	
	sema_init(&ts->fw_sema,0);
	
	rc = gsl_ts_init_ts(client, ts);
	if (rc < 0) {
		dev_err(&client->dev, "GSLX680 init failed\n");
		goto error_mutex_destroy;
	}

#ifdef LATE_UPGRADE

	g_pdata->upgrade_task = kthread_run(gslx680_late_upgrade, NULL, "gslx680_late_upgrade");
	if (!g_pdata->upgrade_task)
		printk("%s creat upgrade process failed\n", __func__);
	else
		printk("%s creat upgrade process sucessful\n", __func__);
#else
	init_chip(ts->client);
	check_mem_data(ts->client);
#endif

	rc = request_irq(client->irq, gsl_ts_irq, IRQF_DISABLED, client->name, ts);
	if (rc < 0) {
		printk( "gsl_probe: request irq failed\n");
		goto error_req_irq_fail;
	}
#ifdef LATE_UPGRADE
	disable_irq(client->irq);
#endif


	/* create debug attribute */
	//rc = device_create_file(&ts->input->dev, &dev_attr_debug_enable);

#ifdef CONFIG_HAS_EARLYSUSPEND
   /*ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1; //EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	ts->early_suspend.suspend = gsl_ts_early_suspend;
	ts->early_suspend.resume = gsl_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
	*/
#endif

    Gts =ts;
	create_init(client->dev, g_pdata);
	printk("[GSLX680] End %s\n", __func__);

	return 0;

//exit_set_irq_mode:	
error_req_irq_fail:
    free_irq(ts->irq, ts);	

error_mutex_destroy:
	mutex_destroy(&ts->sus_lock);
	input_free_device(ts->input);
	kfree(ts);
exit_check_functionality_failed:
	free_touch_gpio(g_pdata);
	ts_com->owner = NULL;
	return rc;
}

static int gsl_ts_remove(struct i2c_client *client)
{
	struct gsl_ts *ts = i2c_get_clientdata(client);
	printk("==gsl_ts_remove=\n");

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	destroy_remove(client->dev, g_pdata);
	device_init_wakeup(&client->dev, 0);
	cancel_work_sync(&ts->work);
	free_irq(ts->irq, ts);
	destroy_workqueue(ts->wq);
	input_unregister_device(ts->input);
	mutex_destroy(&ts->sus_lock);

	//device_remove_file(&ts->input->dev, &dev_attr_debug_enable);
	
	kfree(ts->touch_data);
	kfree(ts);

	return 0;
}

static const struct i2c_device_id gsl_ts_id[] = {
	{GSLX680_I2C_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, gsl_ts_id);


static struct i2c_driver gsl_ts_driver = {
	.driver = {
		.name = GSLX680_I2C_NAME,
		.owner = THIS_MODULE,
	},
	.suspend	= gsl_ts_suspend,
	.resume	= gsl_ts_resume,
    .probe		= gsl_ts_probe,
	.remove		= gsl_ts_remove,
	.id_table	= gsl_ts_id,
};

static int __init gsl_ts_init(void)
{
  int ret;
	printk("==gsl_ts_init==\n");
	ret = i2c_add_driver(&gsl_ts_driver);
	return ret;
}
static void __exit gsl_ts_exit(void)
{
	printk("==gsl_ts_exit==\n");
	i2c_del_driver(&gsl_ts_driver);
	return;
}

module_init(gsl_ts_init);
module_exit(gsl_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GSLX680B touchscreen controller driver");
MODULE_AUTHOR("Guan Yuwei, guanyuwei@basewin.com");
MODULE_ALIAS("platform:gsl_ts");
