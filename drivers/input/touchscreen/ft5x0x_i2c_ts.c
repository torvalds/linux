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
 *    note: only support mulititouch    Wenfs 2010-10-01
 */

#include <linux/irq.h>
#include <asm/mach/irq.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include "ft5x0x_i2c_ts.h"
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <linux/earlysuspend.h>
#include <linux/input/mt.h>

struct FTS_TS_DATA_T {
	struct 	i2c_client *client;
    struct input_dev    *input_dev;
    struct FTS_TS_EVENT_T        event;
    struct work_struct     pen_event_work;
    struct workqueue_struct *ts_workqueue;
    struct 	early_suspend early_suspend;
};

/* -------------- global variable definition -----------*/
static struct i2c_client *this_client;
static REPORT_FINGER_INFO_T _st_finger_infos[CFG_MAX_POINT_NUM];
//static unsigned int _sui_irq_num= IRQ_EINT(6);
static int _si_touch_num = 0; 

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


#ifdef CONFIG_HAS_EARLYSUSPEND
static void ft5x0x_ts_early_suspend(struct early_suspend *h);
static void ft5x0x_ts_late_resume(struct early_suspend *h);
#endif


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
		return FTS_FALSE;
	}

	return FTS_TRUE;
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
		return FTS_FALSE;
	}

	return FTS_TRUE;
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
		return FTS_FALSE;
	}

	/*call the read callback function to get the register value*/		
	if(!i2c_read_interface(rx_buf, rx_length))
	{
		return FTS_FALSE;
	}
	return FTS_TRUE;
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
callback:        report to the input system that the finger is put up;
[parameters]:
null;
[return]:
null;
 ************************************************************************/
static void fts_ts_release(void)
{
	struct FTS_TS_DATA_T *data = i2c_get_clientdata(this_client);
	int i;
	int i_need_sync = 0;
	for ( i= 0; i<CFG_MAX_POINT_NUM; ++i )
	{
		if ( _st_finger_infos[i].u2_pressure == -1 )
			continue;

		_st_finger_infos[i].u2_pressure = 0;

		input_report_abs(data->input_dev, ABS_MT_POSITION_X, _st_finger_infos[i].i2_x);
		input_report_abs(data->input_dev, ABS_MT_POSITION_Y, _st_finger_infos[i].i2_y);
		input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, _st_finger_infos[i].u2_pressure);
		input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, _st_finger_infos[i].ui2_id);
		input_mt_sync(data->input_dev);

		i_need_sync = 1;

		if ( _st_finger_infos[i].u2_pressure == 0 )
			_st_finger_infos[i].u2_pressure= -1;
	}

	if (i_need_sync)
	{
		input_sync(data->input_dev);
	}

	_si_touch_num = 0;
}






/***********************************************************************
  [function]: 
callback:                 read touch  data ftom ctpm by i2c interface;
[parameters]:
rxdata[in]:              data buffer which is used to store touch data;
length[in]:              the length of the data buffer;
[return]:
FTS_TRUE:              success;
FTS_FALSE:             fail;
 ************************************************************************/
static int fts_i2c_rxdata(u8 *rxdata, int length)
{
	int ret;
	struct i2c_msg msg;


	msg.addr = this_client->addr;
	msg.flags = 0;
	msg.len = 1;
	msg.buf = rxdata;
	msg.scl_rate = FT5X0X_I2C_SPEED;
	
	ret = i2c_transfer(this_client->adapter, &msg, 1);

	if(ret == 0){
		pr_err("msg %s line:%d i2c write error: %d\n", __func__, __LINE__,ret);
		return -EBUSY;
	}else if(ret < 0){
		pr_err("msg %s line:%d i2c write error: %d\n", __func__, __LINE__,ret);
		return ret;
	}
	
	msg.addr = this_client->addr;
	msg.flags = I2C_M_RD;
	msg.len = length;
	msg.buf = rxdata;
	msg.scl_rate = FT5X0X_I2C_SPEED;
	ret = i2c_transfer(this_client->adapter, &msg, 1);
	
	if(ret == 0){
		pr_err("msg %s line:%d i2c write error: %d\n", __func__, __LINE__,ret);
		return -EBUSY;
	}else if(ret < 0){
		pr_err("msg %s line:%d i2c write error: %d\n", __func__, __LINE__,ret);
		return ret;
	}

	return ret;
}





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
	msg.scl_rate = FT5X0X_I2C_SPEED;
	
	ret = i2c_transfer(this_client->adapter, &msg, 1);
	
	if(ret == 0){
		pr_err("msg %s line:%d i2c write error: %d\n", __func__, __LINE__,ret);
		return -EBUSY;
	}else if(ret < 0){
		pr_err("msg %s line:%d i2c write error: %d\n", __func__, __LINE__,ret);
		return ret;
	}

	return ret;
}



/***********************************************************************
  [function]: 
callback:            gather the finger information and calculate the X,Y
coordinate then report them to the input system;
[parameters]:
null;
[return]:
null;
 ************************************************************************/
int fts_read_data(void)
{
	struct FTS_TS_DATA_T *data = i2c_get_clientdata(this_client);
	u8 buf[32] = {0};
	static int key_id=0x80;

	int i,id,temp,i_count,ret = -1;
	int touch_point_num = 0, touch_event, x, y, pressure, size;
	REPORT_FINGER_INFO_T touch_info[CFG_MAX_POINT_NUM];


	i_count = 0;

	do 
	{
		buf[0] = 3;

		id = 0xe;  

		ret=fts_i2c_rxdata(buf, 6);
		if (ret > 0)  
		{

			id = buf[2]>>4;
			//printk("\n--the id number is %d---\n",id);
			touch_event = buf[0]>>6;     
			if (id >= 0 && id< CFG_MAX_POINT_NUM)  
			{

				temp = buf[0]& 0x0f;
				temp = temp<<8;
				temp = temp | buf[1];
				x = temp; 

				temp = (buf[2])& 0x0f;
				temp = temp<<8;
				temp = temp | buf[3];
				y=temp;
			#if 1	
				{
					int swap;
				
					//x = (768-x)*600/768;
					//y = y*1024/1024;

					swap = x;
					x = y;
					y = swap;
					
					x = 1024 - x;////////////////////////////
					y = 600 - y;
				}
			#endif

				pressure = buf[4] & 0x3f; 
				size = buf[5]&0xf0;
				size = (id<<8)|size;
				touch_event = buf[0]>>6; 

				if (touch_event == 0)  //press down
				{
					//if(y>=0 && y<850)
					{


						_st_finger_infos[id].u2_pressure= 1;//pressure;
						_st_finger_infos[id].i2_x= (int16_t)x;
						_st_finger_infos[id].i2_y= (int16_t)y;
						_st_finger_infos[id].ui2_id  = size;
						_si_touch_num ++;
					}  
#if 0

					else if(y>=850 && y<=860)
					{
						if (x>=75 && x<=90)
						{
							key_id = 0;
							printk("\n---virtual key 1 press---");		
						}
						else if ( x>=185 && x<=200)
						{
							key_id = 1;
							printk("\n---virtual key 2 press---");		
						}
						else if (x>=290 && x<=305)
						{
							key_id = 2;
							printk("\n---virtual key 3 press---");		
						}
						else if ( x>=405 && x<=420)
						{
							key_id = 3;
							printk("\n---virtual key 4 press---");		
						}


						input_report_key(data->input_dev, tsp_keycodes[key_id], 1);
						tsp_keystatus[key_id] = KEY_PRESS;

					}
#endif
				}   

				else if (touch_event == 1) //up event
				{

					_st_finger_infos[id].u2_pressure= 0;
#if 0
					if(key_id !=0x80)  	
					{    
						i=key_id;
						printk("\n");
						printk("\n---virtual key %d release---\n",++i);
						for(i=0;i<8;i++)
							input_report_key(data->input_dev, tsp_keycodes[key_id], 0);

						key_id=0x80;
					}
#endif
				}

				else if (touch_event == 2) //move
				{
//					printk("[TSP]id=%d move\n", id);
					_st_finger_infos[id].u2_pressure= 1;//pressure;
					_st_finger_infos[id].i2_x= (int16_t)x;
					_st_finger_infos[id].i2_y= (int16_t)y;
					_st_finger_infos[id].ui2_id  = size;
					_si_touch_num ++;
				}
				else					/*bad event, ignore*/
					continue;  

				if ( (touch_event==1) )
				{
//					printk("[TSP]id=%d up\n",  id);  
				}
				
//				printk("\n--report x position  is  %d,pressure=%d----\n",_st_finger_infos[id].i2_x, touch_event);
//				printk("\n--report y position  is  %d,pressure=%d----\n",_st_finger_infos[id].i2_y, touch_event);


				for( i= 0; i<CFG_MAX_POINT_NUM; ++i )
				{
					if(_st_finger_infos[i].down_num > 1000)//5*5*40
						_st_finger_infos[i].u2_pressure = 0;
//					printk("point_idx = [%d],updown=%d,down_num=%d\n",i,_st_finger_infos[i].u2_pressure,_st_finger_infos[i].down_num );
					if(_st_finger_infos[i].u2_pressure == 1)//down
					{
						_st_finger_infos[i].down_num++;
						input_mt_slot(data->input_dev, i);
						input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);					
						input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 1);
						input_report_abs(data->input_dev, ABS_MT_POSITION_X,  _st_finger_infos[i].i2_x);
						input_report_abs(data->input_dev, ABS_MT_POSITION_Y,  _st_finger_infos[i].i2_y);
					}
					else if(_st_finger_infos[i].u2_pressure == 0)//up
					{
						_st_finger_infos[i].down_num = 0;
						input_mt_slot(data->input_dev, i);
						input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, false);
					}
//					else
//						printk("[%s]invalid pressure value %d\n",__FUNCTION__,_st_finger_infos[i].u2_pressure);

//					input_sync(data->input_dev);

					if(_st_finger_infos[i].u2_pressure == 0 )
					{
						_st_finger_infos[i].u2_pressure= -1;
					}

				}

				input_sync(data->input_dev);
/*
				if (_si_touch_num == 0 )
				{
					fts_ts_release();
				}
				_si_touch_num = 0;
*/
			}    

		}	

		else
		{
			printk("[TSP] ERROR: in %s, line %d, ret = %d\n",
					__FUNCTION__, __LINE__, ret);
		}

		i_count ++;
	}while( id != 0xf && i_count < CFG_MAX_POINT_NUM);


	return 0;
}



static void fts_work_func(struct work_struct *work)
{
	fts_read_data();    
	enable_irq(this_client->irq);
}




static irqreturn_t fts_ts_irq(int irq, void *dev_id)
{
	struct FTS_TS_DATA_T *ft5x0x_ts = dev_id;
//	printk(KERN_ALERT "fts_tp_irq\n");
	if (!work_pending(&ft5x0x_ts->pen_event_work)) {
		disable_irq_nosync(this_client->irq);
		queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
	}

	return IRQ_HANDLED;
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





#define    FTS_PACKET_LENGTH        128

static unsigned char CTPM_FW[]=
{
	//#include "ft_app.i"
};




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
		packet_buf[2] = (FTS_BYTE)(temp>>8);
		packet_buf[3] = (FTS_BYTE)temp;
		lenght = FTS_PACKET_LENGTH;
		packet_buf[4] = (FTS_BYTE)(lenght>>8);
		packet_buf[5] = (FTS_BYTE)lenght;

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
		packet_buf[2] = (FTS_BYTE)(temp>>8);
		packet_buf[3] = (FTS_BYTE)temp;

		temp = (dw_lenth) % FTS_PACKET_LENGTH;
		packet_buf[4] = (FTS_BYTE)(temp>>8);
		packet_buf[5] = (FTS_BYTE)temp;

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
		packet_buf[2] = (FTS_BYTE)(temp>>8);
		packet_buf[3] = (FTS_BYTE)temp;
		temp =1;
		packet_buf[4] = (FTS_BYTE)(temp>>8);
		packet_buf[5] = (FTS_BYTE)temp;
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




int fts_ctpm_fw_upgrade_with_i_file(void)
{
	u8*     pbt_buf = FTS_NULL;
	int i_ret;

	pbt_buf = CTPM_FW;
	i_ret =  fts_ctpm_fw_upgrade(pbt_buf,sizeof(CTPM_FW));

	return i_ret;
}

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

void ft5x0x_ts_set_standby(struct i2c_client *client, int enable)
{
    struct laibao_platform_data *mach_info = client->dev.platform_data;
	unsigned pwr_pin = mach_info->pwr_pin;
	unsigned pwr_on_value = mach_info->pwr_on_value;
	unsigned reset_pin = mach_info->reset_pin;
	unsigned reset_value = mach_info->reset_value;

    printk("%s : %s, enable = %d\n", __FILE__, __FUNCTION__,enable);
    if(pwr_pin != INVALID_GPIO)
    {
        gpio_direction_output(pwr_pin, 0);
        gpio_set_value(pwr_pin, enable ? pwr_on_value : !pwr_on_value);				
    }
    if(reset_pin != INVALID_GPIO)
    {
        gpio_direction_output(reset_pin, enable ? reset_value : !reset_value);
        gpio_set_value(reset_pin, enable ? reset_value : !reset_value);				
    }
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ft5x0x_ts_early_suspend(struct early_suspend *h)
{
	struct FTS_TS_DATA_T *data = i2c_get_clientdata(this_client);
	

    printk("enter ft5x0x_ts_early_suspend\n");
	
	cancel_work_sync(&data->pen_event_work);
	
	disable_irq(this_client->irq);

	
	ft5x0x_ts_set_standby(this_client,0);
	
	return;
}
static void ft5x0x_ts_late_resume(struct early_suspend *h)
{
	struct FTS_TS_DATA_T *data = i2c_get_clientdata(this_client);

	ft5x0x_ts_set_standby(this_client,1);

	enable_irq(this_client->irq);

    printk("ft5x0x_ts_late_resume finish\n");

	return ;
}
#else
#define ft5x0x_ts_early_suspend       NULL
#define ft5x0x_ts_late_resume        NULL
#endif


static int fts_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct FTS_TS_DATA_T *ft5x0x_ts;
	struct input_dev *input_dev;
	int err = 0;
	int _sui_irq_num;
	unsigned char reg_value;
	unsigned char reg_version;
	int i;

	struct laibao_platform_data *pdata = client->dev.platform_data;

	client->irq = gpio_to_irq(client->irq);
	_sui_irq_num = client->irq;
	
	printk("[TSP] file(%s), function (%s), --probe start\n", __FILE__, __FUNCTION__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	ft5x0x_ts = kzalloc(sizeof(*ft5x0x_ts), GFP_KERNEL);
	if (!ft5x0x_ts)    {
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	this_client = client;
	ft5x0x_ts->client = client;
	i2c_set_clientdata(client, ft5x0x_ts);

	INIT_WORK(&ft5x0x_ts->pen_event_work, fts_work_func);

	ft5x0x_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
	if (!ft5x0x_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}
	
	if (pdata->init_platform_hw)
		pdata->init_platform_hw();



	/***wait CTP to bootup normally***/
	msleep(200); 

	//fts_register_read(FT5X0X_REG_FIRMID, &reg_version,1);
	i2c_master_reg8_recv(this_client, FT5X0X_REG_FIRMID, &reg_version, 1, 200*1000);
	printk("[TSP] firmware version = 0x%2x\n", reg_version);
	//fts_register_read(FT5X0X_REG_REPORT_RATE, &reg_value,1);
	i2c_master_reg8_recv(this_client, FT5X0X_REG_REPORT_RATE, &reg_value, 1, 200*1000);
	printk("[TSP]firmware report rate = %dHz\n", reg_value*10);
	//fts_register_read(FT5X0X_REG_THRES, &reg_value,1);
	i2c_master_reg8_recv(this_client, FT5X0X_REG_THRES, &reg_value, 1, 200*1000);
	printk("[TSP]firmware threshold = %d\n", reg_value * 4);
	//fts_register_read(FT5X0X_REG_NOISE_MODE, &reg_value,1);
	i2c_master_reg8_recv(this_client, FT5X0X_REG_NOISE_MODE, &reg_value, 1, 200*1000);
	printk("[TSP]nosie mode = 0x%2x\n", reg_value);

#if 0
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
	printk("[TSP]=========================_sui_irq_num = %d   ================\n",_sui_irq_num);
	printk("[TSP]=========================client->dev.driver->name = %s  ================\n",client->dev.driver->name);
	err = request_irq(_sui_irq_num, fts_ts_irq, GPIOEdgelFalling, client->dev.driver->name, ft5x0x_ts);

	if (err < 0) {
		dev_err(&client->dev, "[TSP]ft5x0x_probe: request irq failed\n");
		printk("[TSP]=========================err = %d   ================\n",err);	
		goto exit_irq_request_failed;
	}
	disable_irq(_sui_irq_num);

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "[TSP]failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}

	ft5x0x_ts->input_dev = input_dev;

	/***setup coordinate area******/
	//set_bit(EV_ABS, input_dev->evbit);
	//set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	//set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	//set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	//set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
	
//	input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	__set_bit(EV_ABS, input_dev->evbit);

	/****** for multi-touch *******/
	for (i=0; i<CFG_MAX_POINT_NUM; i++)   
		_st_finger_infos[i].u2_pressure = -1;

	input_set_abs_params(input_dev,
			ABS_MT_POSITION_X, 0, SCREEN_MAX_X + SCREEN_BOUNDARY_ADJUST_VALUE, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y + SCREEN_BOUNDARY_ADJUST_VALUE, 0, 0);
//	input_set_abs_params(input_dev,
//			ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	//input_set_abs_params(input_dev,
	//		ABS_MT_TRACKING_ID, 0, 30, 0, 0);
//	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);

	input_mt_init_slots(input_dev, CFG_MAX_POINT_NUM);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	
	/*****setup key code area******/
	//set_bit(EV_SYN, input_dev->evbit);
	//set_bit(EV_KEY, input_dev->evbit);
	//set_bit(BTN_TOUCH, input_dev->keybit);
	//input_dev->keycode = tsp_keycodes;
	//for(i = 0; i < CFG_NUMOFKEYS; i++)
	//{
	//	input_set_capability(input_dev, EV_KEY, ((int*)input_dev->keycode)[i]);
	//	tsp_keystatus[i] = KEY_RELEASE;
	//}

	input_dev->name        = FT5X0X_NAME;
	input_dev->id.bustype = BUS_I2C;
	input_dev->id.vendor = 0xdead;
	input_dev->id.product = 0xbeef;
	input_dev->id.version = 10427;

	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
				"fts_ts_probe: failed to register input device: %s\n",
				dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}


#ifdef CONFIG_HAS_EARLYSUSPEND
    ft5x0x_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
    ft5x0x_ts->early_suspend.suspend = ft5x0x_ts_early_suspend;
    ft5x0x_ts->early_suspend.resume = ft5x0x_ts_late_resume;
    register_early_suspend(&ft5x0x_ts->early_suspend);
#endif


	enable_irq(_sui_irq_num);    
	printk("[TSP] file(%s), function (%s), -- end\n", __FILE__, __FUNCTION__);
	return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	free_irq(_sui_irq_num, ft5x0x_ts);
exit_irq_request_failed:
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
exit_create_singlethread:
	printk("[TSP] ==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(ft5x0x_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}



static int __devexit fts_ts_remove(struct i2c_client *client)
{
	struct FTS_TS_DATA_T *ft5x0x_ts;
	int _sui_irq_num=client->irq;

	ft5x0x_ts = (struct FTS_TS_DATA_T *)i2c_get_clientdata(client);
	free_irq(_sui_irq_num, ft5x0x_ts);
	input_unregister_device(ft5x0x_ts->input_dev);
	kfree(ft5x0x_ts);
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id ft5x0x_ts_id[] = {
	{FT5X0X_NAME, 0},
	{}
};


MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver fts_ts_driver = {
	.probe	= fts_ts_probe,
	.remove = fts_ts_remove,//devexit_p(fts_ts_remove),
	.id_table = ft5x0x_ts_id,
	.driver = {
		.name = FT5X0X_NAME,
	},
};

static void __init fts_ts_initasync(void *unused, async_cookie_t cookie)
{
	i2c_add_driver(&fts_ts_driver);
}

static int __init fts_ts_init(void)
{
	async_schedule(fts_ts_initasync, NULL);
	return 0;
}

static void __exit fts_ts_exit(void)
{
	i2c_del_driver(&fts_ts_driver);
}

module_init(fts_ts_init);
module_exit(fts_ts_exit);

MODULE_AUTHOR("<duxx@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");

