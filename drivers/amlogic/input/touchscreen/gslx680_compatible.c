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

#include "linux/amlogic/input/gslx680_compatible.h"

#define STAT_POWER_ON		  0
#define STAT_IDLE     	  1
#define STAT_BUSY     	  2
#define STAT_SLEEP 				3

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
struct key_data gsl_key_data[] = {
	{KEY_BACK, 2048, 2048, 2048, 2048},
	{KEY_HOME, 2048, 2048, 2048, 2048},	
	{KEY_MENU, 2048, 2048, 2048, 2048},
	{KEY_SEARCH, 2048, 2048, 2048, 2048},
};
#define MAX_KEY_NUM ARRAY_SIZE(gsl_key_data)
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
	spinlock_t lock;
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *wq;
	struct delayed_work monitor_work;
	struct gsl_ts_data *dd;
	u8 *touch_data;
	u8 device_id;
	int irq;
#ifdef GSL_NOID_VERSION
	struct aml_gsl_api *api;
#endif
	int *config;
	int config_len;
	struct fw_data *fw;
	int fw_len;
	int stat;
};

static u32 id_sign[MAX_CONTACTS+1] = {0};
static u8 id_state_flag[MAX_CONTACTS+1] = {0};
static u8 id_state_old_flag[MAX_CONTACTS+1] = {0};
static u16 x_old[MAX_CONTACTS+1] = {0};
static u16 y_old[MAX_CONTACTS+1] = {0};
static u16 x_new = 0;
static u16 y_new = 0;


static inline u16 join_bytes(u8 a, u8 b)
{
	u16 ab = 0;
	ab = ab | a;
	ab = ab << 8 | b;
	return ab;
}
#if 0
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
#endif
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

static int judge_chip_type(struct i2c_client *client)
{
	u8 read_buf[4]  = {0};
	int chip_type;

	msleep(50);
	if (gsl_ts_read(client,0xfc, read_buf, sizeof(read_buf)) < 0) return -1;

	if(0x82 == read_buf[2]) {
		chip_type = CHIP_1680E;
		printk("chip type: 1680E\n");
	}
	else if(0x36 == read_buf[2]) {
		chip_type = CHIP_3680B;
		printk("chip type: 3680B\n");
	}
	else if (0x88 == read_buf[2]) {
		chip_type = CHIP_3680A;
		printk("chip type: 3680A\n");
	}
	else if (0x91 == read_buf[2]) {
		chip_type = CHIP_3670;
		printk("chip type: 3670\n");
	}
	else {
		chip_type = CHIP_UNKNOWN;
		printk("chip type: %d, unknown\n", read_buf[2]);
	}
	return chip_type;
}

static __inline__ void fw2buf(u8 *buf, const u32 *fw)
{
	u32 *u32_buf = (int *)buf;
	*u32_buf = *fw;
}

static void gsl_load_fw(struct i2c_client *client)
{
	struct gsl_ts *ts = dev_get_drvdata(&(client->dev));
	struct fw_data *ptr_fw = ts->fw; 
	int source_len = ts->fw_len;
	u8 buf[DMA_TRANS_LEN*4 + 1] = {0};
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	
	printk("gslx680: fw loading(%d)...\n", source_len);
	for (source_line = 0; source_line < source_len; source_line++) 
	{
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == ptr_fw[source_line].offset)
		{
			fw2buf(cur, &ptr_fw[source_line].val);
			gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
			send_flag = 1;
		}
		else 
		{
			if (1 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20))
	    			buf[0] = (u8)ptr_fw[source_line].offset;

			fw2buf(cur, &ptr_fw[source_line].val);
			cur += 4;

			if (0 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20)) 
			{
	    			gsl_write_interface(client, buf[0], buf, cur - buf - 1);
	    			cur = buf + 1;
			}
			send_flag++;
		}
	}
	printk("gslx680: fw load complete!\n");
}

#ifdef GSL_FW_FILE
static int gslx680_fill_config_buf(void *priv, int idx, int data)
{
	struct gsl_ts *ts = (struct gsl_ts *)priv;
		
	if (ts->config_len < GSLX680_CONFIG_MAX) {
		ts->config[ts->config_len++] = data;
		return 0;
	}
	else return -ENOMEM;
}

static int gslx680_fill_fw_buf(void *priv, int idx, int data)
{
	struct gsl_ts *ts = (struct gsl_ts *)priv;

	if (ts->fw_len < GSLX680_FW_MAX) {
		if (idx&1)
			ts->fw[ts->fw_len++].val = data;
		else 
			ts->fw[ts->fw_len].offset = data;
		return 0;
	}
	else return -ENOMEM;
}
#endif

static int gslX680_shutdown_low(void)
{
	aml_gpio_direction_output(ts_com->gpio_reset, 0);
	return 0;
}

static int gslX680_shutdown_high(void)
{
	aml_gpio_direction_output(ts_com->gpio_reset, 1);
	return 0;
}

static void startup_chip(struct i2c_client *client)
{
	struct gsl_ts *ts = dev_get_drvdata(&(client->dev));
	u8 tmp = 0x00;

#ifdef GSL_NOID_VERSION
	if (ts->api && ts->api->gsl_DataInit)
		ts->api->gsl_DataInit(ts->config);
#endif
	gsl_ts_write(client, 0xe0, &tmp, 1);
	msleep(10);	
}

static void reset_chip(struct i2c_client *client)
{
	u8 tmp = 0x88;
	u8 buf[4] = {0x00};

	gsl_ts_write(client, 0xe0, &tmp, sizeof(tmp));
	msleep(20);
	tmp = 0x04;
	gsl_ts_write(client, 0xe4, &tmp, sizeof(tmp));
	msleep(10);
	gsl_ts_write(client, 0xbc, buf, sizeof(buf));
	msleep(10);
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

static void init_chip(struct i2c_client *client)
{
	printk("gslx680: init start...\n");
	gslX680_shutdown_low();	
	msleep(20); 	
	gslX680_shutdown_high();	
	msleep(20); 		
	clr_reg(client);
	reset_chip(client);
	gsl_load_fw(client);
	startup_chip(client);	
	reset_chip(client);	
	startup_chip(client);	
	printk("gslx680: init end.\n");
}

static bool check_mem_data_b0(struct i2c_client *client)
{
	u8 buf[4] = {0};
	
	gsl_ts_read(client, 0xb0, buf, sizeof(buf));
	return (buf[0] != 0x5a || buf[1] != 0x5a || buf[2] != 0x5a || buf[2] != 0x5a) ? false : true;

}

#ifdef STRETCH_FRAME
static void stretch_frame(u16 *x, u16 *y)
{
	u16 temp_x = *x;
	u16 temp_y = *y;
	u16 temp_0, temp_1, temp_2;

	if(temp_x < X_STRETCH_MAX + X_STRETCH_CUST)
	{
		temp_0 = temp_1 = temp_2 = 0;
		temp_0 = X_STRETCH_MAX + X_STRETCH_CUST - temp_x;
		temp_0 = temp_0 > X_STRETCH_CUST ? X_STRETCH_CUST : temp_0;
		temp_0 = temp_0*(100 + X_RATIO_CUST)/100;
		if(temp_x < X_STRETCH_MAX)
		{
			temp_1 = X_STRETCH_MAX - temp_x;
			temp_1 = temp_1 > X_STRETCH_MAX/4 ? X_STRETCH_MAX/4 : temp_1;
			temp_1 = temp_1*(100 + 2*XL_RATIO_1)/100;
		}	
		if(temp_x < 3*X_STRETCH_MAX/4)
		{
			temp_2 = 3*X_STRETCH_MAX/4 - temp_x;
			temp_2 = temp_2*(100 + 2*XL_RATIO_2)/100;
		}
		*x = (temp_0 + temp_1 +temp_2) < (X_STRETCH_MAX + X_STRETCH_CUST) ? ((X_STRETCH_MAX + X_STRETCH_CUST) - (temp_0 + temp_1 +temp_2)) : 1;
	}
	else if(temp_x > (CTP_MAX_X -X_STRETCH_MAX - X_STRETCH_CUST))
	{
		temp_0 = temp_1 = temp_2 = 0;
		temp_0 = temp_x - (CTP_MAX_X -X_STRETCH_MAX - X_STRETCH_CUST);
		temp_0 = temp_0 > X_STRETCH_CUST ? X_STRETCH_CUST : temp_0;
		temp_0 = temp_0*(100 + X_RATIO_CUST)/100;
		if(temp_x > (CTP_MAX_X -X_STRETCH_MAX))
		{
			temp_1 = temp_x - (CTP_MAX_X -X_STRETCH_MAX);
			temp_1 = temp_1 > X_STRETCH_MAX/4 ? X_STRETCH_MAX/4 : temp_1;
			temp_1 = temp_1*(100 + 2*XR_RATIO_1)/100;
		}	
		if(temp_x > (CTP_MAX_X -3*X_STRETCH_MAX/4))
		{
			temp_2 = temp_x - (CTP_MAX_X -3*X_STRETCH_MAX/4);
			temp_2 = temp_2*(100 + 2*XR_RATIO_2)/100;
		}
		*x = (temp_0 + temp_1 +temp_2) < (X_STRETCH_MAX + X_STRETCH_CUST) ? ((CTP_MAX_X -X_STRETCH_MAX - X_STRETCH_CUST) + (temp_0 + temp_1 +temp_2)) : (CTP_MAX_X - 1);
	}
		
	if(temp_y < Y_STRETCH_MAX + Y_STRETCH_CUST)
	{
		temp_0 = temp_1 = temp_2 = 0;
		temp_0 = Y_STRETCH_MAX + Y_STRETCH_CUST - temp_y;
		temp_0 = temp_0 > Y_STRETCH_CUST ? Y_STRETCH_CUST : temp_0;
		temp_0 = temp_0*(100 + Y_RATIO_CUST)/100;
		if(temp_y < Y_STRETCH_MAX)
		{
			temp_1 = Y_STRETCH_MAX - temp_y;
			temp_1 = temp_1 > Y_STRETCH_MAX/4 ? Y_STRETCH_MAX/4 : temp_1;
			temp_1 = temp_1*(100 + 2*YL_RATIO_1)/100;
		}	
		if(temp_y < 3*Y_STRETCH_MAX/4)
		{
			temp_2 = 3*Y_STRETCH_MAX/4 - temp_y;
			temp_2 = temp_2*(100 + 2*YL_RATIO_2)/100;
		}
		*y = (temp_0 + temp_1 +temp_2) < (Y_STRETCH_MAX + Y_STRETCH_CUST) ? ((Y_STRETCH_MAX + Y_STRETCH_CUST) - (temp_0 + temp_1 +temp_2)) : 1;
	}
	else if(temp_y > (CTP_MAX_Y -Y_STRETCH_MAX - Y_STRETCH_CUST))
	{
		temp_0 = temp_1 = temp_2 = 0;	
		temp_0 = temp_y - (CTP_MAX_Y -Y_STRETCH_MAX - Y_STRETCH_CUST);
		temp_0 = temp_0 > Y_STRETCH_CUST ? Y_STRETCH_CUST : temp_0;
		temp_0 = temp_0*(100 + Y_RATIO_CUST)/100;
		if(temp_y > (CTP_MAX_Y -Y_STRETCH_MAX))
		{
			temp_1 = temp_y - (CTP_MAX_Y -Y_STRETCH_MAX);
			temp_1 = temp_1 > Y_STRETCH_MAX/4 ? Y_STRETCH_MAX/4 : temp_1;
			temp_1 = temp_1*(100 + 2*YR_RATIO_1)/100;
		}	
		if(temp_y > (CTP_MAX_Y -3*Y_STRETCH_MAX/4))
		{
			temp_2 = temp_y - (CTP_MAX_Y -3*Y_STRETCH_MAX/4);
			temp_2 = temp_2*(100 + 2*YR_RATIO_2)/100;
		}
		*y = (temp_0 + temp_1 +temp_2) < (Y_STRETCH_MAX + Y_STRETCH_CUST) ? ((CTP_MAX_Y -Y_STRETCH_MAX - Y_STRETCH_CUST) + (temp_0 + temp_1 +temp_2)) : (CTP_MAX_Y - 1);
	}
}
#endif

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
	else if(x_err <= 4*FILTER_MAX && y_err <= 4*FILTER_MAX)
	{
		filter_step_x = filter_step_x*3/4; 
		filter_step_y = filter_step_y*3/4;
	}	
	
	x_new = x > x_old[id] ? (x_old[id] + filter_step_x) : (x_old[id] - filter_step_x);
	y_new = y > y_old[id] ? (y_old[id] + filter_step_y) : (y_old[id] - filter_step_y);

	x_old[id] = x_new;
	y_old[id] = y_new;
}
#else
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
#endif

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

#ifdef SLEEP_CLEAR_POINT
static void clear_point(struct gsl_ts *ts)
{
#ifdef REPORT_DATA_ANDROID_4_0
	int i;
	for(i = 1; i <= MAX_CONTACTS ;i ++)
	{
		input_mt_slot(ts->input, i);
		input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
#else
	input_mt_sync(ts->input);
#endif
	input_sync(ts->input);
}
#endif

static void report_data(struct gsl_ts *ts, u16 x, u16 y, u8 pressure, u8 id)
{

	print_info("#####id=%d,x=%d,y=%d######\n",id,x,y);

	if(x > SCREEN_MAX_X || y > SCREEN_MAX_Y)
	{
	#ifdef HAVE_TOUCH_KEY
		report_key(ts,x,y);
	#endif
		return;
	}

	//printk(KERN_ERR"............x = %d               y = %d\n",x,y);

	//x=SCREEN_MAX_X-x;
	
#ifdef REPORT_DATA_ANDROID_4_0
	input_mt_slot(ts->input, id);		
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);	
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
#else
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X,x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
	input_mt_sync(ts->input);
#endif
}

static void gslX680_ts_worker(struct work_struct *work)
{
	int rc, i;
	//u8 id, touches, read_buf[4] = {0};
	u8 id, touches;
	u16 x, y;

	struct gsl_ts *ts = container_of(work, struct gsl_ts,work);
			 
#ifdef GSL_NOID_VERSION
	u32 tmp1 = 0;
	u8 buf[4] = {0};
	struct gsl_touch_info cinfo = {{0},{0},{0},0};
#endif
	print_info("=====gslX680_ts_worker=====\n");	
	rc = gsl_ts_read(ts->client, 0x80, ts->touch_data, ts->dd->data_size);
	if (rc < 0) 
	{
		dev_err(&ts->client->dev, "read failed\n");
		goto schedule;
	}
		
	touches = ts->touch_data[ts->dd->touch_index];
	print_info("-----touches: %d -----\n", touches);		
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
		
	if (ts->api && ts->api->gsl_alg_id_main)
		ts->api->gsl_alg_id_main(&cinfo);
	if (ts->api && ts->api->gsl_mask_tiaoping)
		tmp1 = ts->api->gsl_mask_tiaoping();

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
	
	for(i = 1; i <= MAX_CONTACTS; i ++)
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
			filter_point(x, y ,id);
		#else
			record_point(x, y , id);
		#endif
		if(ts_com->pol & 0x4)
			swap(x_new, y_new);
		if(ts_com->pol & 0x1)
			x_new = SCREEN_MAX_X - x_new;
		if(ts_com->pol & 0x2)
			y_new = SCREEN_MAX_Y - y_new;

		report_data(ts, x_new, y_new, 10, id);
		id_state_flag[id] = 1;
	}


	}
	for(i = 1; i <= MAX_CONTACTS; i ++)
	{	
		if( (0 == touches) || ((0 != id_state_old_flag[i]) && (0 == id_state_flag[i])) )
		{
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
	
schedule:
	enable_irq(ts->irq);
	ts->stat = STAT_IDLE;	
}

static void gsl_monitor_worker(struct work_struct *work)
{
	struct gsl_ts *ts = container_of(to_delayed_work(work), struct gsl_ts, monitor_work);
//	int i;

#ifdef GSL_NOID_VERSION
	if (ts->api == NULL) {
		if ((ts->api = aml_gsl_get_api()) > 0) {
			printk("gslx680: get api OK!\n");
		}
		goto exit_monitor_work;
	}
#endif
#ifdef GSL_FW_FILE
	if (!ts->config_len) {
		if (get_data_from_text_file(ts_com->config_file, gslx680_fill_config_buf, (void*)ts) > 0) {
			printk("gslx680: config length = %d\n", ts->config_len);
		}
		else goto exit_monitor_work;
	}
	if (!ts->fw_len ) {
		if (get_data_from_text_file(ts_com->fw_file, gslx680_fill_fw_buf, (void*)ts) > 0) {
			printk("gslx680: fw length = %d\n", ts->fw_len);
		}
		else goto exit_monitor_work;
	}
#endif
	if (ts->stat == STAT_POWER_ON) {
		init_chip(ts->client);
		ts->stat = STAT_IDLE;
	}
	else if (ts->stat == STAT_SLEEP) {
		gslX680_shutdown_high();
		msleep(20); 	
		reset_chip(ts->client);
		startup_chip(ts->client);
		if (check_mem_data_b0(ts->client) == false) {
			init_chip(ts->client);
		}	
		ts->stat = STAT_IDLE;
	}
	else if (ts->stat == STAT_IDLE) {
		if (check_mem_data_b0(ts->client) == false) {
			printk("gslx680: check mem data NG!\n");
			init_chip(ts->client);
		}
		else {
			printk("gslx680: check mem data OK!\n");
		}
	}

exit_monitor_work:
	if (ts->stat == STAT_POWER_ON) {
		queue_delayed_work(ts->wq, &ts->monitor_work, msecs_to_jiffies(500));
	}
}

static irqreturn_t gsl_ts_irq(int irq, void *dev_id)
{	
	struct gsl_ts *ts = dev_id;
	unsigned long flags;

	spin_lock_irqsave(&ts->lock,flags);
	if (ts->stat == STAT_IDLE) {
		ts->stat = STAT_BUSY;
		disable_irq_nosync(ts->irq);
		queue_work(ts->wq, &ts->work);
	}
	spin_unlock_irqrestore(&ts->lock,flags);
	return IRQ_HANDLED;
}

static int gslX680_register_input(struct i2c_client *client, struct gsl_ts *ts)
{
	struct input_dev *input_device;
	
	input_device = input_allocate_device();
	if (!input_device) return -ENOMEM;
		
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
	input_device->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
#endif

#ifdef HAVE_TOUCH_KEY
	int i;
	input_device->evbit[0] = BIT_MASK(EV_KEY);
	//input_device->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	for (i = 0; i < MAX_KEY_NUM; i++)
		set_bit(gsl_key_data[i].key, input_device->keybit);
#endif

	set_bit(ABS_MT_POSITION_X, input_device->absbit);
	set_bit(ABS_MT_POSITION_Y, input_device->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_device->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_device->absbit);

	input_set_abs_params(input_device,ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_device,ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_device,ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_device,ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);

	if (input_register_device(input_device)) return -ENODEV;
	return 0;
}

static int gsl_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct gsl_ts *ts = dev_get_drvdata(&(client->dev));
	unsigned long flags;

	gslX680_shutdown_low();
	spin_lock_irqsave(&ts->lock,flags);
	cancel_delayed_work_sync(&ts->monitor_work);
	cancel_work_sync(&ts->work);
#ifdef SLEEP_CLEAR_POINT
	clear_point(ts);
	report_data(ts, 1, 1, 10, 1);
	input_sync(ts->input);
#endif
	if (ts->stat == STAT_IDLE) {
		disable_irq_nosync(ts->irq);
	}
	ts->stat = STAT_SLEEP;
	spin_unlock_irqrestore(&ts->lock,flags);
	return 0;
}

static int gsl_ts_resume(struct i2c_client *client)
{
	struct gsl_ts *ts = dev_get_drvdata(&(client->dev));
	unsigned long flags;
	
	spin_lock_irqsave(&ts->lock,flags);
	queue_delayed_work(ts->wq, &ts->monitor_work, msecs_to_jiffies(10));
	enable_irq(ts->irq);
	spin_unlock_irqrestore(&ts->lock,flags);
	return 0;
}

static int gsl_ts_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	struct gsl_ts *ts=NULL;
	int rc = -1;

	printk("GSLX680 Enter %s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C functionality not supported\n");
		return -ENODEV;
	}
 
	if (ts_com->owner != NULL) return -ENODEV;
	memset(ts_com, 0 ,sizeof(struct touch_pdata));
	ts_com = (struct touch_pdata*)client->dev.platform_data;
	printk("ts_com->owner = %s\n", ts_com->owner);
	if (request_touch_gpio(ts_com) != ERR_NO)
		goto err_gslX680_is_not_exist;

	gslX680_shutdown_high();
	msleep(20);
	if ((rc = judge_chip_type(client)) < 0) {
		dev_err(&client->dev, "No GSLX680 chip on the board!\n");
		goto err_gslX680_is_not_exist;
	}
	
	ts_com->hardware_reset = NULL;
	ts_com->read_version = NULL;
	ts_com->upgrade_touch = NULL;
	SCREEN_MAX_X = ts_com->xres;
	SCREEN_MAX_Y = ts_com->yres;
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (!ts) {
		dev_err(&client->dev, "Failed to alloc ts!\n");
		rc = -ENOMEM;
		goto err_gslX680_is_not_exist;
	}
	ts->client = client;
	i2c_set_clientdata(client, ts);
	client->irq = ts_com->irq;
	ts->stat = STAT_POWER_ON;
	spin_lock_init(&ts->lock);
	
#ifdef GSL_FW_FILE
	ts->config = kzalloc(sizeof(int)*GSLX680_CONFIG_MAX, GFP_KERNEL);
	ts->config_len = 0;
	ts->fw = kzalloc(sizeof(struct fw_data)*GSLX680_FW_MAX, GFP_KERNEL);
	ts->fw_len = 0;
	if (!ts->config || !ts->fw) {
		dev_err(&client->dev, "Failed to alloc config & fw!\n");
		rc = -ENOMEM;
		goto err_gslX680_is_not_exist;
	}
#else
	ts->config = GSLX680_CONFIG;
	ts->config_len = ARRAY_SIZE(GSLX680_CONFIG);
	ts->fw = GSLX680_FW;
	ts->fw_len = ARRAY_SIZE(GSLX680_FW);
#endif

	ts->device_id = id->driver_data;
	ts->dd = &devices[ts->device_id];
	ts->dd->data_size = MAX_FINGERS * ts->dd->touch_bytes + ts->dd->touch_meta_data;
	ts->dd->touch_index = 0;
	ts->touch_data = kzalloc(ts->dd->data_size, GFP_KERNEL);
	if (!ts->touch_data) {
		dev_err(&client->dev, "Failed to alloc touch_data!\n");
		rc = -ENOMEM;
		goto err_gslX680_is_not_exist;
	}

	if ((rc = gslX680_register_input(client, ts)) < 0) {
		dev_err(&client->dev, "Failed to register input device!\n");
		goto err_gslX680_is_not_exist;
	}
	
	rc = request_irq(client->irq, gsl_ts_irq, IRQF_DISABLED, client->name, ts);
	if (rc < 0) {
		dev_err(&client->dev, "Failed to request irq(%d)!\n", client->irq);
		goto err_gslX680_is_not_exist;
	}
	ts->irq = client->irq;

	ts->wq = create_singlethread_workqueue("kworkqueue_ts");
	if (!ts->wq) {
		rc = -ENODEV;
		dev_err(&client->dev, "Failed to create workqueue!\n");
		goto err_gslX680_is_not_exist;
	}
	flush_workqueue(ts->wq);
	INIT_WORK(&ts->work, gslX680_ts_worker);
	INIT_DELAYED_WORK(&ts->monitor_work, gsl_monitor_worker);
	queue_delayed_work(ts->wq, &ts->monitor_work, msecs_to_jiffies(1000));

	printk("[GSLX680] End %s\n", __func__);
	return 0;

err_gslX680_is_not_exist:
	free_touch_gpio(ts_com);
	ts_com->owner = NULL;
	if (ts) {
		if (ts->irq) free_irq(ts->irq, ts);
		if (ts->input) input_free_device(ts->input);
		if (ts->touch_data) kfree(ts->config);
#ifdef GSL_FW_FILE
		if (ts->config) kfree(ts->config);
		if (ts->fw) kfree(ts->fw);
#endif
		kfree(ts);
	}
	return rc;
}

static int gsl_ts_remove(struct i2c_client *client)
{
	struct gsl_ts *ts = i2c_get_clientdata(client);
	printk("==gsl_ts_remove=\n");

	cancel_delayed_work_sync(&ts->monitor_work);
	device_init_wakeup(&client->dev, 0);
	cancel_work_sync(&ts->work);
	free_irq(ts->irq, ts);
	destroy_workqueue(ts->wq);
	input_unregister_device(ts->input);
#ifdef GSL_FW_FILE
	if (ts->fw) kfree(ts->fw);
	if (ts->config) kfree(ts->config);
#endif
	kfree(ts->touch_data);
	kfree(ts);
	free_touch_gpio(ts_com);
	ts_com->owner = NULL;
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
	ret = i2c_add_driver(&gsl_ts_driver);
	return ret;
}
static void __exit gsl_ts_exit(void)
{
	i2c_del_driver(&gsl_ts_driver);
	return;
}

module_init(gsl_ts_init);
module_exit(gsl_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GSLX680 touchscreen controller driver");
MODULE_AUTHOR("Guan Yuwei, guanyuwei@basewin.com");
MODULE_ALIAS("platform:gsl_ts");
