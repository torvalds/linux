/* drivers/input/touchscreen/gt818_ts.c
 *
 * Copyright (C) 2011 Goodix, Inc.
 * 
 * Author: Felix
 * Date: 2011.04.28
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <linux/gpio.h>
#include <mach/iomux.h>

#include <linux/irq.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>

#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <asm/uaccess.h>

#include "gt818_ts.h"



#if !defined(GT801_PLUS) && !defined(GT801_NUVOTON)
#error The code does not match this touchscreen.
#endif

static struct workqueue_struct *goodix_wq;

static const char *gt818_ts_name = "Goodix Capacitive TouchScreen";

static struct point_queue finger_list;

struct i2c_client * i2c_connect_client = NULL;

//EXPORT_SYMBOL(i2c_connect_client);

static struct proc_dir_entry *goodix_proc_entry;
	
#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h);
static void goodix_ts_late_resume(struct early_suspend *h);
#endif
//used by firmware update CRC
unsigned int oldcrc32 = 0xFFFFFFFF;
unsigned int crc32_table[256];
unsigned int ulPolynomial = 0x04c11db7;

#ifdef HAVE_TOUCH_KEY
	const uint16_t gt818_key_array[]={
									  KEY_MENU,
									  KEY_HOME,
									  KEY_BACK,
									  KEY_SEARCH
									 };
	#define MAX_KEY_NUM	 (sizeof(gt818_key_array)/sizeof(gt818_key_array[0]))
#endif




/*******************************************************	
鍔熻兘锛�
	璇诲彇浠庢満鏁版嵁
	姣忎釜璇绘搷浣滅敤涓ゆ潯i2c_msg缁勬垚锛岀1鏉℃秷鎭敤浜庡彂閫佷粠鏈哄湴鍧�紝
	绗�鏉＄敤浜庡彂閫佽鍙栧湴鍧�拰鍙栧洖鏁版嵁锛涙瘡鏉℃秷鎭墠鍙戦�璧峰淇″彿
鍙傛暟锛�	client:	i2c璁惧锛屽寘鍚澶囧湴鍧�	buf[0]锛�棣栧瓧鑺備负璇诲彇鍦板潃
	buf[1]~buf[len]锛氭暟鎹紦鍐插尯
	len锛�璇诲彇鏁版嵁闀垮害
return锛�	鎵ц娑堟伅鏁�*********************************************************/
/*Function as i2c_master_send */
static int i2c_read_bytes(struct i2c_client *client, u8 *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret = -1;
	//鍙戦�鍐欏湴鍧�
	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;  //鍐欐秷鎭�
	msgs[0].len = 2;
	msgs[0].buf = &buf[0];
	msgs[0].scl_rate = 400*1000;
	msgs[0].udelay = client->udelay;

	//鎺ユ敹鏁版嵁
	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;//璇绘秷鎭�
	msgs[1].len = len-2;
	msgs[1].buf = &buf[2];
	msgs[1].scl_rate = 400*1000;
	msgs[1].udelay = client->udelay;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if(ret < 0)
		printk("%s:i2c_transfer fail =%d\n",__func__, ret);
	return ret;
}

/*******************************************************	
鍔熻兘锛�	鍚戜粠鏈哄啓鏁版嵁
鍙傛暟锛�	client:	i2c璁惧锛屽寘鍚澶囧湴鍧�	buf[0]锛�棣栧瓧鑺備负鍐欏湴鍧�	buf[1]~buf[len]锛氭暟鎹紦鍐插尯
	len锛�鏁版嵁闀垮害
return锛�	鎵ц娑堟伅鏁�*******************************************************/
/*Function as i2c_master_send */
static int i2c_write_bytes(struct i2c_client *client,u8 *data,int len)
{
	struct i2c_msg msg;
	int ret = -1;
	//鍙戦�璁惧鍦板潃
	msg.addr = client->addr;
	msg.flags = client->flags;   //鍐欐秷鎭�
	msg.len = len;
	msg.buf = data;
	msg.scl_rate = 400*1000;
	msg.udelay = client->udelay;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if(ret < 0)
		printk("%s:i2c_transfer fail =%d\n",__func__, ret);
	return ret;
}

/*******************************************************
鍔熻兘锛�	鍙戦�鍓嶇紑鍛戒护

	ts:	client绉佹湁鏁版嵁缁撴瀯浣�return锛�
	鎵ц缁撴灉鐮侊紝0琛ㄧず姝ｅ父鎵ц
*******************************************************/
static int i2c_pre_cmd(struct gt818_ts_data *ts)
{
	int ret;
	u8 pre_cmd_data[2] = {0};
	pre_cmd_data[0] = 0x0f;
	pre_cmd_data[1] = 0xff;
	ret = i2c_write_bytes(ts->client,pre_cmd_data,2);
	msleep(2);
	return ret;
}

/*******************************************************
鍔熻兘锛�	鍙戦�鍚庣紑鍛戒护

	ts:	client绉佹湁鏁版嵁缁撴瀯浣�return锛�
	鎵ц缁撴灉鐮侊紝0琛ㄧず姝ｅ父鎵ц
*******************************************************/
static int i2c_end_cmd(struct gt818_ts_data *ts)
{
	int ret;
	u8 end_cmd_data[2] = {0};
	end_cmd_data[0] = 0x80;
	end_cmd_data[1] = 0x00;
	ret = i2c_write_bytes(ts->client,end_cmd_data,2);
	msleep(2);
	return ret;
}


/*******************************************************
鍔熻兘锛�	Guitar鍒濆鍖栧嚱鏁帮紝鐢ㄤ簬鍙戦�閰嶇疆淇℃伅锛岃幏鍙栫増鏈俊鎭�鍙傛暟锛�	ts:	client绉佹湁鏁版嵁缁撴瀯浣�return锛�	鎵ц缁撴灉鐮侊紝0琛ㄧず姝ｅ父鎵ц
*******************************************************/
static int goodix_init_panel(struct gt818_ts_data *ts)
{
	int ret = -1;
	#if 1
	u8 config_info[] = {		//Touch key devlop board
	0x06,0xA2,
	0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,
	0x10,0x12,0x00,0x00,0x10,0x00,0x20,0x00,
	0x30,0x00,0x40,0x00,0x50,0x00,0x60,0x00,
	0xE0,0x00,0xD0,0x00,0xC0,0x00,0xB0,0x00,
	0xA0,0x00,0x90,0x00,0x80,0x00,0x70,0x00,
	0x00,0x00,0x01,0x13,0x90,0x90,0x90,0x38,
	0x38,0x38,0x0F,0x0E,0x0A,0x42,0x30,0x08,
	0x03,0x00,MAX_FINGER_NUM,0x00,0x14,0x00,0x1C,0x01,
	0x01,0x3E,0x35,0x68,0x58,0x00,0x00,0x06,
	0x19,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
	0x14,0x10,0x51,0x02,0x00,0x00,0x00,0x00,
	0x00,0x00,0x20,0x40,0x60,0x90,0x08,0x42,
	0x30,0x32,0x20,0x00,0x00,0x00,0x00,0x00,
	0x00,0x01
	};
	#endif
		
	#if 0
	u8 config_info[] = {
	0x06,0xA2,
	0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,
	0x10,0x12,0x70,0x00,0x60,0x00,0x80,0x00,
	0x50,0x00,0x90,0x00,0x40,0x00,0xA0,0x00,
	0x30,0x00,0xB0,0x00,0x20,0x00,0xC0,0x00,
	0x10,0x00,0xD0,0x00,0x00,0x00,0xE0,0x00,
	0x00,0x00,0x01,0x13,0x80,0x88,0x90,0x30,
	0x15,0x40,0x0F,0x0F,0x0A,0x60,0x3C,(0x00|((~(INT_TRIGGER<<3))&0x08)),
	0x03,0x60,MAX_FINGER_NUM,(TOUCH_MAX_WIDTH&0xff),(TOUCH_MAX_WIDTH>>8),(TOUCH_MAX_HEIGHT&0xff),(TOUCH_MAX_HEIGHT>>8),0x00,
	0x00,0x46,0x5A,0x5C,0x5E,0x00,0x00,0x03,
	0x19,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
	0x14,0x10,0x00,0x04,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x38,
	0x00,0x3C,0x28,0x00,0x00,0x00,0x00,0x00,
	0x00,0x01
	};								
	#endif

	ret = i2c_write_bytes(ts->client, config_info, (sizeof(config_info)/sizeof(config_info[0])));
	if (ret < 0) 
		return ret;
	msleep(10);
	return 0;

}


/*******************************************************
鍔熻兘锛�	鑾峰彇鐗堟湰淇℃伅
鍙傛暟锛�	ts:	client绉佹湁鏁版嵁缁撴瀯浣�return锛�	鎵ц缁撴灉鐮侊紝0琛ㄧず姝ｅ父鎵ц
*******************************************************/
static int  goodix_read_version(struct gt818_ts_data *ts)
{
	int ret;
	u8 version_data[5] = {0};	//store touchscreen version infomation
	memset(version_data, 0, 5);
	version_data[0] = 0x07;
	version_data[1] = 0x17;
	msleep(2);
	ret = i2c_read_bytes(ts->client, version_data, 4);
	if (ret < 0) 
		return ret;
	dev_info(&ts->client->dev," Guitar Version: %d.%d\n",version_data[3],version_data[2]);
	return 0;
	
}


/*******************************************************	
鍔熻兘锛�	瑙︽懜灞忓伐浣滃嚱鏁�	鐢变腑鏂Е鍙戯紝鎺ュ彈1缁勫潗鏍囨暟鎹紝鏍￠獙鍚庡啀鍒嗘瀽杈撳嚭
鍙傛暟锛�	ts:	client绉佹湁鏁版嵁缁撴瀯浣�return锛�	鎵ц缁撴灉鐮侊紝0琛ㄧず姝ｅ父鎵ц
********************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{	
	u8  touch_data[3] = {READ_TOUCH_ADDR_H, READ_TOUCH_ADDR_L, 0};
	u8  key_data[3] ={READ_KEY_ADDR_H, READ_KEY_ADDR_L,0};
	u8  point_data[8*MAX_FINGER_NUM+2]={ 0 };
	static u8   finger_last[MAX_FINGER_NUM+1]={0};		//涓婃瑙︽懜鎸夐敭鐨勬墜鎸囩储寮�
	u8  finger_current[MAX_FINGER_NUM+1] = {0};		//褰撳墠瑙︽懜鎸夐敭鐨勬墜鎸囩储寮�
	u8  coor_data[6*MAX_FINGER_NUM] = {0};				//瀵瑰簲鎵嬫寚鐨勬暟鎹�
	static u8  last_key = 0;
	u8  finger = 0;
	u8  key = 0;
	unsigned int  count = 0;
	unsigned int position = 0;	
	int ret=-1;
	int tmp = 0;
	int temp = 0;
	int x = 0, y = 0;
	u16 *coor_point;
	int syn_flag = 0;
	
	struct gt818_ts_data *ts = container_of(work, struct gt818_ts_data, work);

	i2c_pre_cmd(ts);

COORDINATE_POLL:
	if( tmp > 9) {
		dev_info(&(ts->client->dev), "Because of transfer error,touchscreen stop working.\n");
		goto XFER_ERROR ;
	}

	ret = i2c_read_bytes(ts->client, touch_data, sizeof(touch_data)/sizeof(touch_data[0]));  //璇�x712锛岃Е鎽�
	if(ret <= 0) {
		dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
		ts->bad_data = 1;
		tmp ++;
		ts->retry++;
	#ifndef INT_PORT
		goto COORDINATE_POLL;
	#else
		goto XFER_ERROR;
	#endif
	}
	
#ifdef HAVE_TOUCH_KEY	
	ret = i2c_read_bytes(ts->client, key_data, sizeof(key_data)/sizeof(key_data[0]));  //璇�x721锛屾寜閿�
	if(ret <= 0) {
		dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
		ts->bad_data = 1;
		tmp ++;
		ts->retry++;
	#ifndef INT_PORT
		goto COORDINATE_POLL;
	#else
		goto XFER_ERROR;
	#endif
	}
	key = key_data[2] & 0x0f;
#endif

	if(ts->bad_data)
		//TODO:Is sending config once again (to reset the chip) useful?	
		msleep(20);
		
	if((touch_data[2] & 0x30) != 0x20)
	{
		printk("%s:DATA_NO_READY\n", __func__);
		goto DATA_NO_READY;		
	}	

	ts->bad_data = 0;
	
	finger = touch_data[2] & 0x0f;

	if(finger != 0)
	{
		point_data[0] = READ_COOR_ADDR_H;		//read coor high address
		point_data[1] = READ_COOR_ADDR_L;		//read coor low address
		ret = i2c_read_bytes(ts->client, point_data, finger*8+2);
		if(ret <= 0)	
		{
			dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
			ts->bad_data = 1;
			tmp ++;
			ts->retry++;
		#ifndef INT_PORT
			goto COORDINATE_POLL;
		#else
			goto XFER_ERROR;
		#endif
		}		
		for(position=2; position<((finger-1)*8+2+1); position += 8)
		{
			temp = point_data[position];
			if(temp<(MAX_FINGER_NUM+1))
			{
				finger_current[temp] = 1;
				for(count=0; count<6; count++)
				{
					coor_data[(temp-1)*6+count] = point_data[position+1+count];		//璁板綍褰撳墠鎵嬫寚绱㈠紩锛屽苟瑁呰浇鍧愭爣鏁版嵁
				}
			}
			else
			{
				//dev_err(&(ts->client->dev),"Track Id error:%d\n ",);
				ts->bad_data = 1;
				tmp ++;
				ts->retry++;
				#ifndef INT_PORT
					goto COORDINATE_POLL;
				#else
					goto XFER_ERROR;
				#endif
			}		
		}
		//coor_point = (u16 *)coor_data;
	
	}
	
	else
	{
		for(position=1;position < MAX_FINGER_NUM+1; position++)		
		{
			finger_current[position] = 0;
		}
	}
	coor_point = (u16 *)coor_data;
	for(position = 1; position < MAX_FINGER_NUM + 1; position++)
	{
		//printk("%s:positon:%d\n", __func__, position);
		if((finger_current[position] == 0)&&(finger_last[position] != 0))
		{
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 0);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, 0);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(ts->input_dev);
			syn_flag =1;
		}
		else if(finger_current[position])
		{

			x = (*(coor_point+3*(position-1)))*SCREEN_MAX_WIDTH/(TOUCH_MAX_WIDTH);
			y = (*(coor_point+3*(position-1)+1))*SCREEN_MAX_HEIGHT/(TOUCH_MAX_HEIGHT);

			if(x < SCREEN_MAX_WIDTH){
				x = SCREEN_MAX_WIDTH-x;
			}

			if(y < SCREEN_MAX_HEIGHT){
			//	y = SCREEN_MAX_HEIGHT-y;
			}

			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 5);
			input_mt_sync(ts->input_dev);
			syn_flag = 1;
			//printk("x:%d  ", x);
			//printk("y:%d  ", y);
			//printk("\n");
		}
			
	}

	if(syn_flag){
		input_sync(ts->input_dev);
	}

	for(position = 1; position < MAX_FINGER_NUM + 1; position++)
	{
		finger_last[position] = finger_current[position];
	}

#ifdef HAVE_TOUCH_KEY
	if((last_key == 0)&&(key == 0))
		goto DATA_NO_READY;
	else
	{
		for(count = 0; count < 4; count++)
		{
			input_report_key(ts->input_dev, gt818_key_array[count], (key & (0x08 >> count)));
		}
	}		
	last_key = key;	
#endif

DATA_NO_READY:
XFER_ERROR:
	i2c_end_cmd(ts);
	if(ts->use_irq)
		enable_irq(ts->client->irq);

}

/*******************************************************	
鍔熻兘锛�	璁℃椂鍣ㄥ搷搴斿嚱鏁�	鐢辫鏃跺櫒瑙﹀彂锛岃皟搴﹁Е鎽稿睆宸ヤ綔鍑芥暟杩愯锛涗箣鍚庨噸鏂拌鏃�鍙傛暟锛�	timer锛氬嚱鏁板叧鑱旂殑璁℃椂鍣�
return锛�	璁℃椂鍣ㄥ伐浣滄ā寮忥紝HRTIMER_NORESTART琛ㄧず涓嶉渶瑕佽嚜鍔ㄩ噸鍚�********************************************************/
static enum hrtimer_restart goodix_ts_timer_func(struct hrtimer *timer)
{
	struct gt818_ts_data *ts = container_of(timer, struct gt818_ts_data, timer);
	queue_work(goodix_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, (POLL_TIME+6)*1000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

/*******************************************************	
鍔熻兘锛�	涓柇鍝嶅簲鍑芥暟
	鐢变腑鏂Е鍙戯紝璋冨害瑙︽懜灞忓鐞嗗嚱鏁拌繍琛�鍙傛暟锛�	timer锛氬嚱鏁板叧鑱旂殑璁℃椂鍣�
return锛�	璁℃椂鍣ㄥ伐浣滄ā寮忥紝HRTIMER_NORESTART琛ㄧず涓嶉渶瑕佽嚜鍔ㄩ噸鍚�********************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct gt818_ts_data *ts = dev_id;
	disable_irq_nosync(ts->client->irq);
	queue_work(goodix_wq, &ts->work);
	return IRQ_HANDLED;
}

/*******************************************************	
鍔熻兘锛�	绠＄悊GT801鐨勭數婧愶紝鍏佽GT801 PLUS杩涘叆鐫＄湢鎴栧皢鍏跺敜閱�鍙傛暟锛�
on:	0琛ㄧず浣胯兘鐫＄湢锛�涓哄敜閱�return锛�	鏄惁璁剧疆鎴愬姛锛�涓烘垚鍔�
閿欒鐮侊細-1涓篿2c閿欒锛�2涓篏PIO閿欒锛�EINVAL涓哄弬鏁皁n閿欒
********************************************************/
//#if defined(INT_PORT)
static int goodix_ts_power(struct gt818_ts_data * ts, int on)
{
	int ret = -1;
	struct gt818_platform_data	*pdata = ts->client->dev.platform_data;
	unsigned char i2c_control_buf[3] = {0x06,0x92,0x01};		//suspend cmd
	
	#ifdef INT_PORT	
	if(ts != NULL && !ts->use_irq)
		return -2;
#endif		
	switch(on)
	{
		case 0:
			i2c_pre_cmd(ts);
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 3);
			printk(KERN_INFO"Send suspend cmd\n");
			if(ret > 0)						//failed
				ret = 0;
//			i2c_end_cmd(ts);
			return ret;
			
		case 1:
		//	#ifdef INT_PORT	
		//		gpio_direction_output(INT_PORT, 0);
		//		msleep(20);
		//		gpio_free(INT_PORT);
		//		s3c_gpio_setpull(INT_PORT, S3C_GPIO_PULL_NONE);
		//		if(ts->use_irq) 
		//			s3c_gpio_cfgpin(INT_PORT, INT_CFG);	//Set IO port as interrupt port	
		//		else 
		//			gpio_direction_input(INT_PORT);
		//	#else
				gpio_direction_output(pdata->gpio_reset, 0);
				msleep(1);
				gpio_direction_input(pdata->gpio_reset);
		//	#endif					
				msleep(40);
				ret = 0;
				return ret;
				
		default:
			printk(KERN_DEBUG "%s: Cant't support this command.", gt818_ts_name);
			return -EINVAL;
	}

}
/*******************************************************	
鍔熻兘锛�	瑙︽懜灞忔帰娴嬪嚱鏁�	鍦ㄦ敞鍐岄┍鍔ㄦ椂璋冪敤(瑕佹眰瀛樺湪瀵瑰簲鐨刢lient)锛�	鐢ㄤ簬IO,涓柇绛夎祫婧愮敵璇凤紱璁惧娉ㄥ唽锛涜Е鎽稿睆鍒濆鍖栫瓑宸ヤ綔
鍙傛暟锛�	client锛氬緟椹卞姩鐨勮澶囩粨鏋勪綋
	id锛氳澶嘔D
return锛�	鎵ц缁撴灉鐮侊紝0琛ㄧず姝ｅ父鎵ц
********************************************************/
static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	//TODO:鍦ㄦ祴璇曞け璐ュ悗闇�閲婃斁ts
	int ret = 0;
	int retry=0;
	u8 goodix_id[3] = {0,0xff,0};
	struct gt818_ts_data *ts;

	struct gt818_platform_data *pdata;
	dev_dbg(&client->dev,"Install touch driver.\n");
	printk("gt818: Install touch driver.\n");
	//Check I2C function
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	i2c_connect_client = client;	//used by Guitar_Update

	pdata = client->dev.platform_data;

	//init int and reset ports
#ifdef INT_PORT

	ret = gpio_request(INT_PORT, "TS_INT");	//Request IO
	if (ret){
		dev_err(&client->dev, "Failed to request GPIO:%d, ERRNO:%d\n",(int)INT_PORT, ret);
		goto err_gpio_request_failed;
	}
	rk29_mux_api_set(pdata->pendown_iomux_name, pdata->pendown_iomux_mode);
	gpio_direction_input(INT_PORT);
	gpio_pull_updown(INT_PORT, 0);
	//gpio_set_value(INT_PORT, GPIO_HIGH);
#endif

	ret = gpio_request(pdata->gpio_reset, "gt818_resetPin");
	if(ret){
		dev_err(&client->dev, "failed to request resetPin GPIO%d\n", pdata->gpio_reset);
		goto err_gpio_request_failed;
	}
	rk29_mux_api_set(pdata->resetpin_iomux_name, pdata->resetpin_iomux_mode);

	gpio_direction_output(pdata->gpio_reset, 0);
	msleep(1);
	gpio_direction_input(pdata->gpio_reset);
	gpio_pull_updown(pdata->gpio_reset, 0);
	msleep(20);


	while(0){

		for(retry = 0; retry < 3; retry++)
		{
			gpio_direction_output(pdata->gpio_reset, 0);
			msleep(1);
			gpio_direction_input(pdata->gpio_reset);
			gpio_pull_updown(pdata->gpio_reset, 0);
			msleep(20);
			ret = i2c_write_bytes(client, "hhb", 3);	 //Test I2C connection.
			if (ret > 0)
				break;
			msleep(500);
		}
	gpio_direction_output(pdata->gpio_reset, 1);

	}

	if(ret <= 0)
	{
//		dev_err(&client->dev, "Warnning: I2C communication might be ERROR!\n");
		//goto err_i2c_failed;
	}	
	
	INIT_WORK(&ts->work, goodix_ts_work_func);		//init work_struct
	ts->client = client;
	i2c_set_clientdata(client, ts);
	
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_dbg(&client->dev,"goodix_ts_probe: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	//ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE); 						// absolute coor (x,y)
//	ts->input_dev->absbit[0] = BIT(ABS_MT_POSITION_X) | BIT(ABS_MT_POSITION_Y) |
//			BIT(ABS_MT_TOUCH_MAJOR) | BIT(ABS_MT_WIDTH_MAJOR);  // for android

#ifdef HAVE_TOUCH_KEY
	for(retry = 0; retry < MAX_KEY_NUM; retry++)
	{
		input_set_capability(ts->input_dev, EV_KEY, gt818_key_array[retry]);
	}
#endif

	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", dev_name(&client->dev));
	snprintf(ts->name, sizeof(ts->name), "gt818-touchscreen");
//	sprintf(ts->phys, "input/ts");
	ts->input_dev->name = "gt818_ts";//ts->name;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->dev.parent = &client->dev;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;	//screen firmware version

#ifdef GOODIX_MULTI_TOUCH
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, SCREEN_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, SCREEN_MAX_HEIGHT, 0, 0);
#else
	input_set_abs_params(ts->input_dev, ABS_X, 0, SCREEN_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, SCREEN_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
#endif	
	

	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	ts->bad_data = 0;
//	finger_list.length = 0;

#ifdef INT_PORT	

	client->irq = TS_INT;		//If not defined in client
	if (client->irq)
	{

	#if INT_TRIGGER==0
		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_EDGE_RISING
	#elif INT_TRIGGER==1
		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_EDGE_FALLING
	#elif INT_TRIGGER==2
		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_LEVEL_LOW
	#elif INT_TRIGGER==3
		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_LEVEL_HIGH
	#endif

		ret  = request_irq(client->irq, goodix_ts_irq_handler ,  GT801_PLUS_IRQ_TYPE,
			client->name, ts);
		if (ret != 0) {
			dev_err(&client->dev,"Cannot allocate ts INT!ERRNO:%d\n", ret);
			gpio_direction_input(INT_PORT);
			gpio_free(INT_PORT);
			goto err_gpio_request_failed;
		}
		else 
		{	
			disable_irq(client->irq);
			ts->use_irq = 1;
			dev_dbg(&client->dev,"Reques EIRQ %d succesd on GPIO:%d\n",TS_INT,INT_PORT);
		}	
	}
#endif	
err_gpio_request_failed:
	
	if (!ts->use_irq) 
	{
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = goodix_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

	i2c_pre_cmd(ts);
	msleep(2);
	
	for(retry = 0; retry < 3; retry++)
	{
		ret = goodix_init_panel(ts);
		dev_info(&client->dev,"the config ret is :%d\n",ret);
		msleep(2);
		if(ret != 0)	//Initiall failed
			continue;
		else
			break;
	}
	if(ret != 0) {
		ts->bad_data = 1;
		goto err_init_godix_ts;
	}


//	gpio_direction_output(INT_PORT, 1);
//	msleep(1);

	if(ts->use_irq)
		enable_irq(client->irq);
		
	ts->power = goodix_ts_power;

	goodix_read_version(ts);
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	i2c_end_cmd(ts);
	dev_info(&client->dev,"Start %s in %s mode\n", 
		ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");
	return 0;

err_init_godix_ts:
	i2c_end_cmd(ts);
	if(ts->use_irq)
	{
		ts->use_irq = 0;
		free_irq(client->irq,ts);
	#ifdef INT_PORT	
		gpio_direction_input(INT_PORT);
		gpio_free(INT_PORT);
	#endif	
	}
	else 
		hrtimer_cancel(&ts->timer);

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	i2c_set_clientdata(client, NULL);
err_i2c_failed:	
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
err_create_proc_entry:
	return ret;
}


/*******************************************************	
鍔熻兘锛�	椹卞姩璧勬簮閲婃斁
鍙傛暟锛�	client锛氳澶囩粨鏋勪綋
return锛�	鎵ц缁撴灉鐮侊紝0琛ㄧず姝ｅ父鎵ц
********************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	struct gt818_ts_data *ts = i2c_get_clientdata(client);
	struct gt818_platform_data	*pdata = client->dev.platform_data;

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	remove_proc_entry("goodix-update", NULL);
#endif
	if (ts && ts->use_irq) 
	{
	#ifdef INT_PORT
		gpio_direction_input(INT_PORT);
		gpio_free(INT_PORT);
	#endif	
		free_irq(client->irq, ts);
	}	
	else if(ts)
		hrtimer_cancel(&ts->timer);
	
	dev_notice(&client->dev,"The driver is removing...\n");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

//鍋滅敤璁惧
static int goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct gt818_ts_data *ts = i2c_get_clientdata(client);

	if (ts->use_irq)
		disable_irq(client->irq);
	else
		hrtimer_cancel(&ts->timer);
	//ret = cancel_work_sync(&ts->work);
	//if(ret && ts->use_irq)	
		//enable_irq(client->irq);
	if (ts->power) {	/* 蹇呴』鍦ㄥ彇娑坵ork鍚庡啀鎵ц锛岄伩鍏嶅洜GPIO瀵艰嚧鍧愭爣澶勭悊浠ｇ爜姝诲惊鐜�*/
		ret = ts->power(ts, 0);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_resume power off failed\n");
	}
	return 0;
}

//閲嶆柊鍞ら啋
static int goodix_ts_resume(struct i2c_client *client)
{
	int ret;
	struct gt818_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power) {
		ret = ts->power(ts, 1);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_resume power on failed\n");
	}

	if (ts->use_irq)
		enable_irq(client->irq);
	else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h)
{
	struct gt818_ts_data *ts;
	ts = container_of(h, struct gt818_ts_data, early_suspend);
	goodix_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void goodix_ts_late_resume(struct early_suspend *h)
{
	struct gt818_ts_data *ts;
	ts = container_of(h, struct gt818_ts_data, early_suspend);
	goodix_ts_resume(ts->client);
}
#endif


//鍙敤浜庤椹卞姩鐨�璁惧鍚嶁�璁惧ID 鍒楄〃
//only one client
static const struct i2c_device_id goodix_ts_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};

//璁惧椹卞姩缁撴瀯浣�
static struct i2c_driver goodix_ts_driver = {
	.probe		= goodix_ts_probe,
	.remove		= goodix_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= goodix_ts_suspend,
	.resume		= goodix_ts_resume,
#endif
	.id_table	= goodix_ts_id,
	.driver = {
		.name	= GOODIX_I2C_NAME,
		.owner = THIS_MODULE,
	},
};

/*******************************************************	
鍔熻兘锛�	椹卞姩鍔犺浇鍑芥暟
return锛�	鎵ц缁撴灉鐮侊紝0琛ㄧず姝ｅ父鎵ц
********************************************************/
static int __devinit goodix_ts_init(void)
{
	int ret;
	goodix_wq = create_singlethread_workqueue("goodix_wq");		//create a work queue and worker thread
	if (!goodix_wq) {
		printk(KERN_ALERT "creat workqueue faiked\n");
		return -ENOMEM;
	}
	ret = i2c_add_driver(&goodix_ts_driver);
	return ret; 
}

/*******************************************************	
鍔熻兘锛�	椹卞姩鍗歌浇鍑芥暟
鍙傛暟锛�	client锛氳澶囩粨鏋勪綋
********************************************************/
static void __exit goodix_ts_exit(void)
{
	printk(KERN_ALERT "Touchscreen driver of guitar exited.\n");
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);		//release our work queue
}

late_initcall(goodix_ts_init);				//鏈�悗鍒濆鍖栭┍鍔╢elix
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");
