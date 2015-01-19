/* 
 * drivers/input/touchscreen/ft5x02_ts.c
 *
 * FocalTech ft5x02 TouchScreen driver. 
 *
 * Copyright (c) 2012  Focal tech Ltd.
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

#include <linux/i2c.h>
#include <linux/input.h>
#include "ft5x02_ts.h"
#include <linux/earlysuspend.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>   
#include <mach/irqs.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include "linux/amlogic/input/common.h"
extern struct touch_pdata *ts_com;
struct ts_event {
	u16 au16_x[CFG_MAX_TOUCH_POINTS];	/*x coordinate */
	u16 au16_y[CFG_MAX_TOUCH_POINTS];	/*y coordinate */
	u8 au8_touch_event[CFG_MAX_TOUCH_POINTS];	/*touch event:
					0 -- down; 1-- contact; 2 -- contact */
	u8 au8_finger_id[CFG_MAX_TOUCH_POINTS];	/*touch ID */
	u16 pressure;
	u8 touch_point;
};

struct ft5x02_ts_data {
	unsigned int irq;
	unsigned int x_max;
	unsigned int y_max;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct ts_event event;
#ifdef CONFIG_OF
	struct touch_pdata *pdata;
#else
	struct ft5x02_platform_data *pdata;
#endif	
//	struct delayed_work dwork;
#ifdef CONFIG_PM
	struct early_suspend *early_suspend;
#endif
};

#if CFG_SUPPORT_TOUCH_KEY
int tsp_keycodes[CFG_NUMOFKEYS] ={
        KEY_MENU,
        KEY_HOME,
        KEY_BACK,
        KEY_SEARCH
};
char *tsp_keyname[CFG_NUMOFKEYS] ={
        "Menu",
        "Home",
        "Back",
        "Search"
};
static bool tsp_keystatus[CFG_NUMOFKEYS];
#endif

static u8 CTPM_FW[] = 
{
	//#include "ft5x02_app.i"
};

extern int ft5x02_Init_IC_Param(struct i2c_client * client);
extern int ft5x02_get_ic_param(struct i2c_client * client);


#define SYSFS_DEBUG
#ifdef SYSFS_DEBUG
static struct mutex g_device_mutex;
static int ft5x02_create_sysfs_debug(struct i2c_client *client);
#endif
/*
*ft5x02_i2c_Read-read data and write data by i2c
*@client: handle of i2c
*@writebuf: Data that will be written to the slave
*@writelen: How many bytes to write
*@readbuf: Where to store data read from slave
*@readlen: How many bytes to read
*
*Returns negative errno, else the number of messages executed
*
*
*/
int ft5x02_i2c_Read(struct i2c_client *client,  char * writebuf, int writelen, 
							char *readbuf, int readlen)
{
	int ret;

	if(writelen > 0)
	{
		struct i2c_msg msgs[] = {
			{
				.addr	= client->addr,
				.flags	= 0,
				.len	= writelen,
				.buf	= writebuf,
			},
			{
				.addr	= client->addr,
				.flags	= I2C_M_RD,
				.len	= readlen,
				.buf	= readbuf,
			},
		};
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret < 0)
			pr_err("function:%s. i2c read error: %d\n", __func__, ret);
	}
	else
	{
		struct i2c_msg msgs[] = {
			{
				.addr	= client->addr,
				.flags	= I2C_M_RD,
				.len	= readlen,
				.buf	= readbuf,
			},
		};
		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0)
			pr_err("function:%s. i2c read error: %d\n", __func__, ret);
	}
	return ret;
}
/*
*write data by i2c 
*/
int ft5x02_i2c_Write(struct i2c_client *client, char *writebuf, int writelen)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= writelen,
			.buf	= writebuf,
		},
	};

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0)
		pr_err("%s i2c write error: %d\n", __func__, ret);

	return ret;
}

static void delay_qt_ms(unsigned long  w_ms)
{
	unsigned long i;
	unsigned long j;

	for (i = 0; i < w_ms; i++)
	{
		for (j = 0; j < 1000; j++)
		{
			 udelay(1);
		}
	}
}

/*release the point*/
static void ft5x02_ts_release(struct ft5x02_ts_data *data)
{
	input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
	input_sync(data->input_dev);
}

int ft5x02_write_reg(struct i2c_client * client, u8 regaddr, u8 regvalue)
{
	unsigned char buf[2] = {0};
	buf[0] = regaddr;
	buf[1] = regvalue;

	return ft5x02_i2c_Write(client, buf, sizeof(buf));
}

int ft5x02_read_reg(struct i2c_client * client, u8 regaddr, u8 * regvalue)
{
	return ft5x02_i2c_Read(client, &regaddr, 1, regvalue, 1);
}

void ft5x02_upgrade_send_head(struct i2c_client * client)
{
	u8 ret = 0;
	u8 headbuf[2];
	headbuf[0] = 0xFA;
	headbuf[1] = 0xFA;

	ret = ft5x02_i2c_Write(client, headbuf, 2);
	if(ret < 0)
		dev_err(&client->dev, "[FTS]--upgrading, send head error\n");
}
/*
*/
#define FTS_PACKET_LENGTH 128
static int  ft5x02_ctpm_fw_upgrade(struct i2c_client * client, u8* pbt_buf, u32 dw_lenth)
{
	
	u8 reg_val[2] = {0};
	u32 i = 0;

	u32  packet_number;
	u32  j;
	u32  temp;
	u32  lenght;
	u8	packet_buf[FTS_PACKET_LENGTH + 6];
	u8	auc_i2c_write_buf[10];
	u8	bt_ecc;
//	int 	 i_ret;
	struct timeval begin_tv, end_tv;
	do_gettimeofday(&begin_tv);

	/*********Step 1:Reset	CTPM *****/
	/*write 0xaa to register 0x3c*/
	ft5x02_write_reg(client, 0xfc, 0xaa);
	//delay_qt_ms(58);
	msleep(30);
	//do_gettimeofday(&end_tv);
	//DBG("cost time=%lu.%lu\n", begin_tv.tv_sec-end_tv.tv_sec, 
	//		begin_tv.tv_usec-end_tv.tv_usec);
	 /*write 0x55 to register 0x3c*/
	ft5x02_write_reg(client, 0xfc, 0x55);
	//delay_qt_ms(18);
	delay_qt_ms(25);

	/*********Step 2:Enter upgrade mode *****/
	#if 0
	auc_i2c_write_buf[0] = 0x55;
	auc_i2c_write_buf[1] = 0xaa;
	do
	{
		i ++;
		i_ret = ft5x02_i2c_Write(client, auc_i2c_write_buf, 2);
		delay_qt_ms(5);
	}while(i_ret <= 0 && i < 5 );
	#else
	auc_i2c_write_buf[0] = 0x55;
	ft5x02_i2c_Write(client, auc_i2c_write_buf, 1);
	delay_qt_ms(1);
	auc_i2c_write_buf[0] = 0xaa;
	ft5x02_i2c_Write(client, auc_i2c_write_buf, 1);
	#endif

	/*********Step 3:check READ-ID***********************/	 
	delay_qt_ms(10);
	for (i=0; i<3; i++) {
		ft5x02_upgrade_send_head(client);
		auc_i2c_write_buf[0] = 0x90;
		auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] = 0x00;

		ft5x02_i2c_Read(client, auc_i2c_write_buf, 4, reg_val, 2);
		
		if (reg_val[0] == 0x79 
			&& reg_val[1] == 0x02) {
			//dev_dbg(&client->dev, "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
			pr_info("[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
			break;
		} else {
			dev_err(&client->dev, "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
			delay_qt_ms(100);
		}
	}
	if (i >= 3)
		return -EIO;
	/********Step 4:enable write function*/
	ft5x02_upgrade_send_head(client);
	auc_i2c_write_buf[0] = 0x06;
	ft5x02_i2c_Write(client, auc_i2c_write_buf, 1);

	/*********Step 5:write firmware(FW) to ctpm flash*********/
	bt_ecc = 0;

	dw_lenth = dw_lenth - 6;
	packet_number = (dw_lenth) / FTS_PACKET_LENGTH;

	packet_buf[0] = 0xbf;
	packet_buf[1] = 0x00;
	for (j=0; j<packet_number; j++) {
		temp = j * FTS_PACKET_LENGTH;
		packet_buf[2] = (u8)(temp>>8);
		packet_buf[3] = (u8)temp;
		lenght = FTS_PACKET_LENGTH;
		packet_buf[4] = (u8)(lenght>>8);
		packet_buf[5] = (u8)lenght;

		for (i=0; i<FTS_PACKET_LENGTH; i++) {
			packet_buf[6+i] = pbt_buf[j*FTS_PACKET_LENGTH + i]; 
			bt_ecc ^= packet_buf[6+i];
		}
		ft5x02_upgrade_send_head(client);
		ft5x02_i2c_Write(client, packet_buf, FTS_PACKET_LENGTH+6);
		delay_qt_ms(1);
	}

	if ((dw_lenth) % FTS_PACKET_LENGTH > 0) {
		temp = packet_number * FTS_PACKET_LENGTH;
		packet_buf[2] = (u8)(temp>>8);
		packet_buf[3] = (u8)temp;

		temp = (dw_lenth) % FTS_PACKET_LENGTH;
		packet_buf[4] = (u8)(temp>>8);
		packet_buf[5] = (u8)temp;

		for (i=0; i<temp; i++) {
			packet_buf[6+i] = pbt_buf[ packet_number*FTS_PACKET_LENGTH + i]; 
			bt_ecc ^= packet_buf[6+i];
		}
		ft5x02_upgrade_send_head(client);
		ft5x02_i2c_Write(client, packet_buf, temp+6);
		delay_qt_ms(1);
	}

	/*send the last six byte*/
	for (i = 0; i<6; i++) {
		temp = 0x6ffa + i;
		packet_buf[2] = (u8)(temp>>8);
		packet_buf[3] = (u8)temp;
		temp =1;
		packet_buf[4] = (u8)(temp>>8);
		packet_buf[5] = (u8)temp;
		packet_buf[6] = pbt_buf[ dw_lenth + i]; 
		bt_ecc ^= packet_buf[6];

		ft5x02_upgrade_send_head(client);
		ft5x02_i2c_Write(client, packet_buf, 7);
		delay_qt_ms(1);
	}

	/********Disable write function*/
	ft5x02_upgrade_send_head(client);
	auc_i2c_write_buf[0] = 0x04;
	ft5x02_i2c_Write(client, auc_i2c_write_buf, 1);
	/*********Step 6: read out checksum***********************/
	ft5x02_upgrade_send_head(client);
	auc_i2c_write_buf[0] = 0xcc;
	ft5x02_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 1); 

	if (reg_val[0] != bt_ecc) {
		dev_err(&client->dev, "[FTS]--ecc error! FW=%02x bt_ecc=%02x\n", reg_val[0], bt_ecc);
		return -EIO;
	}

	/*********Step 7: reset the new FW***********************/
	ft5x02_upgrade_send_head(client);
	auc_i2c_write_buf[0] = 0x07;
	ft5x02_i2c_Write(client, auc_i2c_write_buf, 1);
	msleep(200);  /*make sure CTP startup normally*/
	//DBG("-------upgrade successful-----\n");

	do_gettimeofday(&end_tv);
	DBG("cost time=%lu.%lu\n", end_tv.tv_sec-begin_tv.tv_sec, 
			end_tv.tv_usec-begin_tv.tv_usec);
	
	return 0;
}

/*
upgrade with *.i file
*/
#define READ_COUNT 5
static int fts_ctpm_fw_upgrade_with_i_file(struct i2c_client * client)
{
	u8 *pbt_buf = NULL;
	int i_ret;
	int fw_len = 0;
	
#ifdef CONFIG_OF	
	int file_size;
	u8 tmp[READ_COUNT];
	u8 check_dot[2];
 	u32 offset = 0;
 
	file_size = touch_open_fw(ts_com->fw_file);
	if(file_size < 0)
	{
		printk("%s: no fw file\n", ts_com->owner);
		return -EIO;
	}
	pbt_buf = vmalloc(200*1024*sizeof(*pbt_buf));
	
	while (offset < file_size) {
		touch_read_fw(offset, READ_COUNT, &tmp[0]);
		i_ret = sscanf(&tmp[0],"0x%c%c",&check_dot[0],&check_dot[1]);
		if (i_ret == 2) {
		  if (check_dot[1] == ',')
				sscanf(&tmp[0],"0x%x, ",(uint *)&pbt_buf[fw_len]);
		  else
				sscanf(&tmp[0],"0x%x,",(uint *)&pbt_buf[fw_len]);
			fw_len++;	
		}
		offset++;
	}
#else
	fw_len = sizeof(CTPM_FW);
	pbt_buf = CTPM_FW;	
#endif
	/*judge the fw that will be upgraded
	 * if illegal, then stop upgrade and return.
	*/
	if (fw_len<8 || fw_len>32*1024) {
		dev_err(&client->dev, "[FTS]----FW length error\n");
#ifdef CONFIG_OF	
		vfree(pbt_buf);
#endif
		return -EIO;
	}	
//	if((CTPM_FW[fw_len-8]^CTPM_FW[fw_len-6])==0xFF
//		&& (CTPM_FW[fw_len-7]^CTPM_FW[fw_len-5])==0xFF
//		&& (CTPM_FW[fw_len-3]^CTPM_FW[fw_len-4])==0xFF)
	{
		/*FW upgrade*/
		//pbt_buf = CTPM_FW;
		/*call the upgrade function*/
		i_ret =  ft5x02_ctpm_fw_upgrade(client, pbt_buf, fw_len);
#ifdef CONFIG_OF	
		vfree(pbt_buf);
#endif
		if (i_ret != 0)
			dev_err(&client->dev, "[FTS]---- upgrade failed. err=%d.\n", i_ret);
		else
			dev_dbg(&client->dev, "[FTS]----upgrade successful\n");
	}
//	else
//	{
//		dev_err(&client->dev, "[FTS]----FW format error\n");
//		return -EBADFD;
//	}
	return i_ret;
}



/* 
*Read touch point information when the interrupt  is asserted.
*/
static int ft5x02_read_Touchdata(struct ft5x02_ts_data *data)
{
	struct ts_event *event = &data->event;
	u8 buf[POINT_READ_BUF] = { 0 };
	int ret = -1;
	int i = 0;
	u8 pointid = FT_MAX_ID;

	ret = ft5x02_i2c_Read(data->client, buf, 1, buf, POINT_READ_BUF);
	if (ret < 0) {
		dev_err(&data->client->dev, "%s read touchdata failed.\n",
			__func__);
		return ret;
	}
	memset(event, 0, sizeof(struct ts_event));

	event->touch_point = 0;
	for (i = 0; i < CFG_MAX_TOUCH_POINTS; i++) {
		pointid = (buf[FT_TOUCH_ID_POS + FT_TOUCH_STEP * i]) >> 4;
		if (pointid >= FT_MAX_ID)
			break;
		else
			event->touch_point++;
		event->au16_x[i] =
		    (s16) (buf[FT_TOUCH_X_H_POS + FT_TOUCH_STEP * i] & 0x0F) <<
		    8 | (s16) buf[FT_TOUCH_X_L_POS + FT_TOUCH_STEP * i];
		event->au16_y[i] =
		    (s16) (buf[FT_TOUCH_Y_H_POS + FT_TOUCH_STEP * i] & 0x0F) <<
		    8 | (s16) buf[FT_TOUCH_Y_L_POS + FT_TOUCH_STEP * i];
		event->au8_touch_event[i] =
		    buf[FT_TOUCH_EVENT_POS + FT_TOUCH_STEP * i] >> 6;
		event->au8_finger_id[i] =
		    (buf[FT_TOUCH_ID_POS + FT_TOUCH_STEP * i]) >> 4;
	}

	event->pressure = FT_PRESS;

	return 0;
}

#if CFG_SUPPORT_TOUCH_KEY
/* 
*Processes the key message when the CFG_SUPPORT_TOUCH_KEY has defined
*/
int ft5x02_touch_key_process(struct input_dev *dev, int x, int y, int touch_event)
{
	int i;
	int key_id;

	if ( y < 517&&y > 497)
	{
		key_id = 1;
	}
	else if ( y < 367&&y > 347)
	{
		key_id = 0;
	}

	else if ( y < 217&&y > 197)
	{
		key_id = 2;
	}  
	else if (y < 67&&y > 47)
	{
		key_id = 3;
	}
	else
	{
		key_id = 0xf;
	}
    
	for(i = 0; i <CFG_NUMOFKEYS; i++ )
	{
		if(tsp_keystatus[i])
		{
			if(touch_event == 1)
			{
				input_report_key(dev, tsp_keycodes[i], 0);
				tsp_keystatus[i] = KEY_RELEASE;
			}
		}
		else if( key_id == i )
		{
			if( touch_event == 0)                                  // detect
			{
				input_report_key(dev, tsp_keycodes[i], 1);
				tsp_keystatus[i] = KEY_PRESS;
			}
		}
	}
	return 0;
    
}    
#endif

/*
*report the point information
*/
static void ft5x02_report_value(struct ft5x02_ts_data *data)
{
	struct ts_event *event = &data->event;
	int i;

	for (i  = 0; i < event->touch_point; i++)
	{
		// LCD view area
	    	if (event->au16_x[i] < data->x_max && event->au16_y[i] < data->y_max)
	    	{
	        	input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->au16_x[i]);
    			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->au16_y[i]);
    			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
    			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->au8_finger_id[i]);
    			if (event->au8_touch_event[i]== 0 || event->au8_touch_event[i] == 2)
    			{
    		    		input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
    			}
    			else
    			{
    		    		input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
    			}
	    	}
	    	else /*maybe the touch key area*/
	    	{
#if CFG_SUPPORT_TOUCH_KEY
            		if (event->au16_x[i] >= data->x_max)
            		{
                		ft5x02_touch_key_process(data->input_dev, event->au16_x[i], event->au16_y[i], event->au8_touch_event[i]);
            		}
#endif
	    	}
	    
			
		input_mt_sync(data->input_dev);
	}
	input_sync(data->input_dev);

    	if (event->touch_point == 0) {
        	ft5x02_ts_release(data);
    	}
}	/*end ft5x02_report_value*/

#if 0
static  void ft5x02_ts_worker(struct work_struct *work)
{
	struct ft5x02_ts_data *ft5x02_ts;
	int ret = 0;
	ft5x02_ts = container_of(work, struct ft5x02_ts_data, dwork.work);
	
	//disable_irq(ft5x02_ts->irq);
	ret = ft5x02_read_Touchdata(ft5x02_ts);
	if (ret == 0)
		ft5x02_report_value(ft5x02_ts);
}
#endif
/*
 * The ft5x02 device will signal the host about TRIGGER_FALLING. 
 */

static irqreturn_t ft5x02_ts_interrupt(int irq, void *dev_id)
{
	struct ft5x02_ts_data *ft5x02_ts = dev_id;
	int ret = 0;
	disable_irq_nosync(ft5x02_ts->irq);
#if 1
	ret = ft5x02_read_Touchdata(ft5x02_ts);
	if (ret == 0)
		ft5x02_report_value(ft5x02_ts);
#else
	cancel_delayed_work(&ft5x02_ts->dwork);
	schedule_delayed_work(&ft5x02_ts->dwork, 0);
#endif
	enable_irq(ft5x02_ts->irq);
	return IRQ_HANDLED;
}

static int ft5x02_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ft5x02_ts_data *ft5x02_ts;
	struct input_dev *input_dev;
	int err = 0;
//	unsigned char uc_reg_value; 
//	unsigned char uc_reg_addr;
#ifdef CONFIG_OF
	struct touch_pdata *pdata = (struct touch_pdata *)client->dev.platform_data;
	if (ts_com->owner != NULL) return -ENODEV;
	memset(ts_com, 0 ,sizeof(struct touch_pdata));
	ts_com = pdata;
	printk("ts_com->owner = %s\n", ts_com->owner);
	if (request_touch_gpio(ts_com) != ERR_NO)
		goto exit_get_dt_failed;
	aml_gpio_direction_output(ts_com->gpio_reset, 0);
	msleep(20);
	//gpio_set_value(ts->pdata->reset, 1);
	aml_gpio_direction_output(ts_com->gpio_reset, 1);
#else
	struct ft5x02_platform_data *pdata = (struct ft5x02_platform_data *)client->dev.platform_data;
#endif
#if CFG_SUPPORT_TOUCH_KEY
    	int i;
#endif
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	ft5x02_ts = kzalloc(sizeof(struct ft5x02_ts_data), GFP_KERNEL);

	if (!ft5x02_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	i2c_set_clientdata(client, ft5x02_ts);
	ft5x02_ts->irq = client->irq;
	ft5x02_ts->client = client;
	ft5x02_ts->pdata = pdata;
#ifdef CONFIG_OF
	ft5x02_ts->x_max = pdata->xres - 1;
	ft5x02_ts->y_max = pdata->yres - 1;
#else	
	ft5x02_ts->x_max = pdata->x_max - 1;
	ft5x02_ts->y_max = pdata->y_max - 1;
#endif	

//	INIT_DELAYED_WORK(&ft5x02_ts->dwork, ft5x02_ts_worker);
#ifdef CONFIG_PM
//	err = gpio_request(pdata->reset, "ft5x02 reset");
//	if (err < 0) {
//		dev_err(&client->dev, "%s:failed to set gpio reset.\n",
//			__func__);
//		goto exit_request_reset;
//	}
#endif
	//aml_gpio_direction_input(ts_com->gpio_interrupt);
  //aml_gpio_to_irq(ts_com->gpio_interrupt, ts_com->irq-INT_GPIO_0, ts_com->irq_edge);
	err = request_threaded_irq(client->irq, ft5x02_ts_interrupt, NULL,
				   IRQF_DISABLED, client->dev.driver->name,
				   ft5x02_ts);
	if (err < 0) {
		dev_err(&client->dev, "ft5x02_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}
	disable_irq(client->irq);

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}
	
	ft5x02_ts->input_dev = input_dev;

	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);

	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_X, 0, ft5x02_ts->x_max, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_Y, 0, ft5x02_ts->y_max, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
    	input_set_abs_params(input_dev,
			     ABS_MT_TRACKING_ID, 0, CFG_MAX_TOUCH_POINTS, 0, 0);


    	set_bit(EV_KEY, input_dev->evbit);
    	set_bit(EV_ABS, input_dev->evbit);

#if CFG_SUPPORT_TOUCH_KEY
    	/*setup key code area*/
    	set_bit(EV_SYN, input_dev->evbit);
    	set_bit(BTN_TOUCH, input_dev->keybit);
    	input_dev->keycode = tsp_keycodes;
    	for(i = 0; i < CFG_NUMOFKEYS; i++)
    	{
        	input_set_capability(input_dev, EV_KEY, ((int*)input_dev->keycode)[i]);
        	tsp_keystatus[i] = KEY_RELEASE;
    	}
#endif

	input_dev->name		= FT5X02_NAME;
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
				"ft5x02_ts_probe: failed to register input device: %s\n",
				dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

   	 msleep(150);  /*make sure CTP already finish startup process*/
#if 1
	/*upgrade for program the app to RAM*/
	dev_dbg(&client->dev, "[FTS]----ready for upgrading---\n");
	if (fts_ctpm_fw_upgrade_with_i_file(client) < 0) {
		dev_err(&client->dev, "[FTS]-----upgrade failed!----\n");
	}
	else
		dev_dbg(&client->dev, "[FTS]-----upgrade successful!----\n");
//#else

	//ft5x02_get_ic_param(client);
	ft5x02_Init_IC_Param(client);
	//DBG("----------get after param---------------\n");
	//ft5x02_get_ic_param(client);
#endif

#ifdef SYSFS_DEBUG
	ft5x02_create_sysfs_debug(client);
#endif
//	cancel_delayed_work(&ft5x02_ts->dwork);
//	schedule_delayed_work(&ft5x02_ts->dwork, 0);
	enable_irq(client->irq);
    	return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
	
exit_input_dev_alloc_failed:
	free_irq(client->irq, ft5x02_ts);
#ifdef CONFIG_PM
exit_request_reset:
	//gpio_free(ft5x02_ts->pdata->reset);
#endif	
exit_irq_request_failed:
	i2c_set_clientdata(client, NULL);
	kfree(ft5x02_ts);

exit_alloc_data_failed:
exit_check_functionality_failed:
exit_get_dt_failed:
	free_touch_gpio(ts_com);
	ts_com->owner = NULL;
	printk("%s: probe failed!\n", __FUNCTION__);	
	return err;
}

#ifdef CONFIG_PM
static void ft5x02_ts_suspend(struct early_suspend *handler)
{
	struct ft5x02_ts_data *ts = container_of(handler, struct ft5x02_ts_data,
						early_suspend);

	dev_dbg(&ts->client->dev, "[FTS]ft5x02 suspend\n");
	disable_irq(ts->pdata->irq);
}

static void ft5x02_ts_resume(struct early_suspend *handler)
{
	struct ft5x02_ts_data *ts = container_of(handler, struct ft5x02_ts_data,
						early_suspend);

	dev_dbg(&ts->client->dev, "[FTS]ft5x02 resume.\n");
	//gpio_set_value(ts->pdata->reset, 0);
	aml_gpio_direction_output(ts_com->gpio_reset, 0);
	msleep(20);
	//gpio_set_value(ts->pdata->reset, 1);
	aml_gpio_direction_output(ts_com->gpio_reset, 1);
	enable_irq(ts->pdata->irq);
}
#else
#define ft5x02_ts_suspend	NULL
#define ft5x02_ts_resume		NULL
#endif


#ifdef SYSFS_DEBUG
static ssize_t ft5x02_initparam_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t num_read_chars = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	mutex_lock(&g_device_mutex);
	if (ft5x02_Init_IC_Param(client) >= 0)
		num_read_chars = sprintf(buf, "%s",
			"ft5x02 init param successful\r\n");
	else
		num_read_chars = sprintf(buf, "%s",
			"ft5x02 init param failed!\r\n");
	//ft5x02_get_ic_param(client);
	mutex_unlock(&g_device_mutex);
	
	return num_read_chars;
}


static ssize_t ft5x02_initparam_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	/*place holder for future use*/
	return -EPERM;
}

static DEVICE_ATTR(ft5x02initparam, S_IRUGO | S_IWUSR, ft5x02_initparam_show,
		   ft5x02_initparam_store);

/*add your attr in here*/
static struct attribute *ft5x02_attributes[] = {
	&dev_attr_ft5x02initparam.attr,
	NULL
};
static struct attribute_group ft5x02_attribute_group = {
	.attrs = ft5x02_attributes
};
static int ft5x02_create_sysfs_debug(struct i2c_client *client)
{
	int err = 0;
	err = sysfs_create_group(&client->dev.kobj, &ft5x02_attribute_group);
	if (0 != err) {
		dev_err(&client->dev,
					 "%s() - ERROR: sysfs_create_group() failed.\n",
					 __func__);
		sysfs_remove_group(&client->dev.kobj, &ft5x02_attribute_group);
		return -EIO;
	} else {
		mutex_init(&g_device_mutex);
		pr_info("ft5x0x:%s() - sysfs_create_group() succeeded.\n",
				__func__);
	}
	return err;
}
#endif

static int ft5x02_ts_remove(struct i2c_client *client)
{
	struct ft5x02_ts_data *ft5x02_ts;
	ft5x02_ts = i2c_get_clientdata(client);
	input_unregister_device(ft5x02_ts->input_dev);
	free_touch_gpio(ts_com);
	#ifdef CONFIG_PM
	//gpio_free(ft5x02_ts->pdata->reset);
	#endif
	#ifdef SYSFS_DEBUG
	mutex_destroy(&g_device_mutex);
	sysfs_remove_group(&client->dev.kobj, &ft5x02_attribute_group);
	#endif
	//cancel_delayed_work_sync(&ft5x02_ts->dwork);
	free_irq(client->irq, ft5x02_ts);
	kfree(ft5x02_ts);
	i2c_set_clientdata(client, NULL); 	
	return 0;
}


static const struct i2c_device_id ft5x02_ts_id[] = {
	{ FT5X02_NAME, 0 },{ }
};

MODULE_DEVICE_TABLE(i2c, ft5x02_ts_id);

static struct i2c_driver ft5x02_ts_driver = {
	.probe		= ft5x02_ts_probe,
	.remove		= ft5x02_ts_remove,
	.id_table	= ft5x02_ts_id,
	.suspend = ft5x02_ts_suspend,
	.resume = ft5x02_ts_resume,
	.driver	= {
		.name	= FT5X02_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init ft5x02_ts_init(void)
{
	int ret;
	ret = i2c_add_driver(&ft5x02_ts_driver);
	if (ret) {
		printk(KERN_WARNING "Adding ft5x02 driver failed "
		       "(errno = %d)\n", ret);
	} else {
		pr_info("Successfully added driver %s\n",
			  ft5x02_ts_driver.driver.name);
	}
	return ret;
}

static void __exit ft5x02_ts_exit(void)
{
	i2c_del_driver(&ft5x02_ts_driver);
}

module_init(ft5x02_ts_init);
module_exit(ft5x02_ts_exit);

MODULE_AUTHOR("<luowj>");
MODULE_DESCRIPTION("FocalTech ft5x02 TouchScreen driver");
MODULE_LICENSE("GPL");
