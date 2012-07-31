/*
 * 
 * Copyright (C) 2011 Goodix, Inc.
 * 
 * Author: Scott
 * Date: 2012.01.05
 */
 

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <mach/gpio.h>
//#include <plat/gpio-cfg.h>
#include <linux/irq.h>
#include <linux/err.h>
#include<linux/workqueue.h>
#include<linux/slab.h>
#include <linux/goodix_touch_82x.h>    //w++

#include <linux/input/mt.h>

#define READ_TOUCH_ADDR_H   0x0F
#define READ_TOUCH_ADDR_L   0x40
#define READ_KEY_ADDR_H     0x0F
#define READ_KEY_ADDR_L     0x41
#define READ_COOR_ADDR_H    0x0F
#define READ_COOR_ADDR_L    0x42
#define RESOLUTION_LOC      71
#define TRIGGER_LOC         66
//#typedef s32 int    //w++


struct goodix_ts_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	bool use_irq;
	bool use_reset;
	bool init_finished;
	struct hrtimer timer;
	struct work_struct  work;
	char phys[32];
	int bad_data;
	int retry;
	s32 (*power)(struct goodix_ts_data * ts, s32 on);
	struct early_suspend early_suspend;
	int xmax;
	int ymax;
	bool swap_xy;
	bool xpol;
	bool ypol;
	int irq_is_disable;
	int interrupt_port;
	int reset_port;
	
};
struct goodix_ts_data *ts82x_temp;		//w++






static struct workqueue_struct *goodix_wq;
static const char *goodix_ts_name ="Goodix TouchScreen of Guitar ";//"Goodix Capacitive TouchScreen";

static s32 goodix_ts_remove(struct i2c_client *);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h);
static void goodix_ts_late_resume(struct early_suspend *h);
#endif

#ifdef CREATE_WR_NODE
extern s32 init_wr_node(struct i2c_client*);
extern void uninit_wr_node(void);
#endif

#ifdef AUTO_UPDATE_GUITAR
extern s32 init_update_proc(struct goodix_ts_data *);
#else
static void guitar_reset( struct goodix_ts_data *ts,u8 ms);
#endif

static int err_gt82x = 0;   //w++记载有没有此设备的检测
//#define LONGPRESS_LOCK_SPECKEY   //ÊÇ·ñÊ¹ÓÃ³€°ŽÄ³Œü(ÈçsearchŒü)Ëø×¡¹ŠÄÜŒüµÄ¹ŠÄÜ
#ifdef LONGPRESS_LOCK_SPECKEY
#define KEY_LOCK KEY_F13
//#define ORIGIN_KEY KEY_MENU
#define LOCK_LONG_PRESS_CNT 20
static int Origin2LockPressCnt = 0;
static int lockflag =0;
//static int touch_key_hold_press = 0;

ssize_t glock_status_show_gt82x(struct device *dev, char *buf)
{
	return sprintf(buf, "%d", lockflag);
}
struct timer_list longkey_timer_82x;
#endif
#ifdef LONGPRESS_LOCK_SPECKEY
static DRIVER_ATTR(get_lock_status, 0777, glock_status_show_gt82x, NULL);
#endif
/*
struct goodix_i2c_rmi_platform_data {
    uint32_t version;    // Use this entry for panels with //
    //reservation
   
};
*/

#if 0
#define TOUCH_MAX_HEIGHT   1024   //w++2
#define TOUCH_MAX_WIDTH     768
#else
#define AUTO_SET
u16 TOUCH_MAX_HEIGHT;
u16 TOUCH_MAX_WIDTH;
#endif

#define GT828_I2C_RATE 200000


/*******************************************************	
���ܣ�	
	��ȡ�ӻ����
	ÿ��������������i2c_msg��ɣ���1����Ϣ���ڷ��ʹӻ��ַ��
	��2�����ڷ��Ͷ�ȡ��ַ��ȡ����ݣ�ÿ����Ϣǰ������ʼ�ź�
����
	client:	i2c�豸�����豸��ַ
	buf[0]~buf[1]��	 ���ֽ�Ϊ��ȡ��ַ
	buf[2]~buf[len]����ݻ�����
	len��	��ȡ��ݳ���
return��
	ִ����Ϣ��
*********************************************************/
/*Function as i2c_master_send */

static s32 i2c_read_bytes(struct i2c_client *client, u8 *buf, s32 len)
{
    struct i2c_msg msgs[2];
    s32 ret=-1;

    //����д��ַ
    msgs[0].flags=!I2C_M_RD; //д��Ϣ
    msgs[0].addr=client->addr;
    msgs[0].len=2;
    msgs[0].buf=&buf[0];
    msgs[0].scl_rate=100000;
    msgs[0].udelay=2000;
    //�������
    msgs[1].flags=I2C_M_RD;//����Ϣ
    msgs[1].addr=client->addr;
    msgs[1].len=len - ADDR_LENGTH;
    msgs[1].buf=&buf[2];
    msgs[1].scl_rate=100000;
    msgs[1].udelay=2000;	

    ret=i2c_transfer(client->adapter,msgs, 2);

    return ret;
}

/*******************************************************	
���ܣ�
	��ӻ�д���
����
	client:	i2c�豸�����豸��ַ
	buf[0]~buf[1]��	 ���ֽ�Ϊд��ַ
	buf[2]~buf[len]����ݻ�����
	len��	��ݳ���	
return��
	ִ����Ϣ��
*******************************************************/
/*Function as i2c_master_send */

static s32 i2c_write_bytes(struct i2c_client *client,u8 *data,s32 len)
{
    struct i2c_msg msg;
    s32 ret=-1;
    
    //�����豸��ַ
    msg.flags=!I2C_M_RD;//д��Ϣ
    msg.addr=client->addr;
    msg.len=len;
    msg.buf=data;        
    msg.scl_rate=100000;
    msg.udelay=2000;
    ret=i2c_transfer(client->adapter,&msg, 1);

    return ret;
}

/*******************************************************
���ܣ�
	����ǰ׺����
	
	ts:	client˽����ݽṹ��
return��
    �ɹ�����1
*******************************************************/
static s32 i2c_pre_cmd(struct goodix_ts_data *ts)
{
    s32 ret;
    u8 pre_cmd_data[2]={0x0f, 0xff};

    ret=i2c_write_bytes(ts->client,pre_cmd_data,2);
    return ret;//*/
}

/*******************************************************
���ܣ�
	���ͺ�׺����
	
	ts:	client˽����ݽṹ��
return��
    �ɹ�����1
*******************************************************/
static s32 i2c_end_cmd(struct goodix_ts_data *ts)
{
    s32 ret;
    u8 end_cmd_data[2]={0x80, 0x00};    

    ret=i2c_write_bytes(ts->client,end_cmd_data,2);
    return ret;//*/
}

/*******************************************************
���ܣ�
	Guitar��ʼ���������ڷ���������Ϣ����ȡ�汾��Ϣ
����
	ts:	client˽����ݽṹ��
return��
	ִ�н���룬0��ʾ��ִ��
*******************************************************/
s32 goodix_init_panel(struct goodix_ts_data *ts, u8 send)
{
    s32 ret = -1;
    u8 config_info[]=
    {
        0x0F,0x80,/*config address*/
#if 1	
// 300-N3216E-A00		
//0x02,0x11,0x03,0x12,0x04,0x13,0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x11,0x07,0x10,0x06,0x0F,0x05,0x0E,0x04,0x0D,0x03,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0F,0x03,0xE0,0x10,0x10,0x21,0x00,0x00,0x09,0x00,0x00,0x02,0x45,0x2D,0x1C,0x03,0x00,0x05,0x00,0x02,0x58,0x03,0x20,0x25,0x29,0x27,0x2B,0x25,0x00,0x06,0x19,0x25,0x14,0x10,0x00,0x13,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
//300-N3788E-A00-V1.0             1024*768
//0x02,0x11,0x03,0x12,0x04,0x13,0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,0x0A,0x19,0xFF,0x13,0xFF,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x12,0x08,0x11,0x07,0x10,0x06,0x0F,0x05,0x0E,0x04,0x0D,0x03,0xFF,0x00,0xFF,0x0E,0x0F,0x10,0x11,0x12,0x09,0x03,0x88,0x88,0x88,0x25,0x00,0x00,0x08,0x00,0x00,0x02,0x3C,0x28,0x1C,0x03,0x00,0x05,0x00,0x02,0x58,0x03,0x20,0x33,0x38,0x30,0x35,0x25,0x00,0x25,0x19,0x05,0x14,0x10,0x02,0x30,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
0x02,0x11,0x03,0x12,0x04,0x13,0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,0x0A,0x19,0x0B,0x1A,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x12,0x08,0x11,0x07,0x10,0x06,0x0F,0x05,0x0E,0x04,0x0D,0x03,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0F,0x03,0xE0,0x10,0x10,0x19,0x00,0x00,0x08,0x00,0x00,0x02,0x45,0x2D,0x1C,0x03,0x00,0x05,0x00,0x02,0x58,0x03,0x20,0x2D,0x38,0x2F,0x3B,0x25,0x00,0x06,0x19,0x25,0x14,0x10,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
#else
0x02,0x11,0x03,0x12,0x04,0x13,0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,0x0A,0x19,0x0B,0x1A,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x12,0x08,0x11,0x07,0x10,0x06,0x0F,0x05,0x0E,0x04,0x0D,0x03,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0F,0x03,0xE0,0x10,0x10,0x19,0x00,0x00,0x08,0x00,0x00,0x02,0x45,0x2D,0x1C,0x03,0x00,0x05,0x00,0x02,0x58,0x03,0x20,0x2D,0x38,0x2F,0x3B,0x25,0x00,0x06,0x19,0x25,0x14,0x10,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
#endif
    };

#ifdef AUTO_SET
    TOUCH_MAX_WIDTH  = ((config_info[RESOLUTION_LOC] << 8)|config_info[RESOLUTION_LOC + 1]);
    TOUCH_MAX_HEIGHT = ((config_info[RESOLUTION_LOC + 2] << 8)|config_info[RESOLUTION_LOC + 3]);

    GTDEBUG_MSG("TOUCH_MAX_WIDTH  : 0x%d\n", (s32)TOUCH_MAX_WIDTH);
    GTDEBUG_MSG("TOUCH_MAX_HEIGHT : 0x%d\n", (s32)TOUCH_MAX_HEIGHT);
#else
/*    config_info[RESOLUTION_LOC]     = TOUCH_MAX_WIDTH >> 8;
    config_info[RESOLUTION_LOC + 1] = TOUCH_MAX_WIDTH & 0xff;
    config_info[RESOLUTION_LOC + 2] = TOUCH_MAX_HEIGHT >> 8;
    config_info[RESOLUTION_LOC + 3] = TOUCH_MAX_HEIGHT & 0xff;*/
#endif

    if (INT_TRIGGER == GT_IRQ_FALLING)
    {
        config_info[TRIGGER_LOC] &= 0xf7; 
    }
    else if (INT_TRIGGER == GT_IRQ_RISING)
    {
        config_info[TRIGGER_LOC] |= 0x08;
    }

    if (send)
    {
        ret=i2c_write_bytes(ts->client,config_info, (sizeof(config_info)/sizeof(config_info[0])));
        if (ret <= 0)
        {
            return fail;
        }
        i2c_end_cmd(ts);
        msleep(10);
    }
    return success;
}

static s32 touch_num(u8 value, s32 max)
{
    s32 tmp = 0;

    while((tmp < max) && value)
    {
        if ((value & 0x01) == 1)
        {
            tmp++;
        }
        value = value >> 1;
    }

    return tmp;
}

/*******************************************************	
���ܣ�
	��������������
	���жϴ���������1�������ݣ�У����ٷ������
����
	ts:	client˽����ݽṹ��
return��
    void
********************************************************/
#ifdef LONGPRESS_LOCK_SPECKEY   //w++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
static void open_longkey_timer_82x(unsigned long data)
{
    if((++Origin2LockPressCnt>LOCK_LONG_PRESS_CNT)&&(Origin2LockPressCnt<LOCK_LONG_PRESS_CNT+2))
		{  //w++超过一个时间,发长按消息
				Origin2LockPressCnt = LOCK_LONG_PRESS_CNT + 3;
				//if(lockflag ==0)  {
					input_report_key(ts82x_temp->input_dev, KEY_LOCK, 1);
					lockflag = (lockflag)? 0 : 1;
					input_sync(ts82x_temp->input_dev);
				//}
		}
		longkey_timer_82x.expires=jiffies+msecs_to_jiffies(100);
    add_timer(&longkey_timer_82x);
		printk("w++++++++   Origin2LockPressCnt = %d\n",Origin2LockPressCnt); 
		printk("lockflag = %d\n",lockflag);
}
#endif

static void goodix_ts_work_func(struct work_struct *work)
{
    u8 finger = 0;
    u8 chk_sum = 0;
    u8 key = 0;
    static u8 last_key = 0;
    u16 X_value;
    u16 Y_value;
	u16 value_tmp;
    u32 count = 0;
    u32 position = 0;
    s32 ret = -1;
    s32 tmp = 0;
    s32 i;
    u8 *coor_point;
    u8 touch_data[2 + 2 + 5*MAX_FINGER_NUM + 1] = {READ_TOUCH_ADDR_H,READ_TOUCH_ADDR_L,0, 0};
    static u8 finger_last[MAX_FINGER_NUM]={0};        //�ϴδ����������ָ����
    u8 finger_current[MAX_FINGER_NUM] = {0};        //��ǰ�����������ָ����

    struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);
    struct i2c_client *client = ts->client;
	struct goodix_i2c_rmi_platform_data *pdata = client->dev.platform_data;
	
#ifndef INT_PORT
COORDINATE_POLL:
#endif
    if( tmp > 9)
    {
        dev_info(&(ts->client->dev), "Because of transfer error,touchscreen stop working.\n");
        goto XFER_ERROR ;
    }

    //���齫���һ���Զ�ȡ��
    ret=i2c_read_bytes(ts->client, touch_data,sizeof(touch_data)/sizeof(touch_data[0])); 
    i2c_end_cmd(ts);
    if(ret <= 0) 
    {
        dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
        ts->bad_data = 1;
        tmp ++;
#ifndef INT_PORT
        goto COORDINATE_POLL;
#else
        goto XFER_ERROR;
#endif
    }

    if(ts->bad_data)
    {
        //TODO:Is sending config once again (to reset the chip) useful?    
        ts->bad_data = 0;
        msleep(20);
    }

    if((touch_data[2]&0xC0)!=0x80)
    {
        goto DATA_NO_READY;        
    }


    key = touch_data[3]&0x0f; // 1, 2, 4, 8
    if (key == 0x0f)
    {
        if (fail == goodix_init_panel(ts, 1))
        {
/**/        GTDEBUG_COOR("Reload config failed!\n");
        }
        else
        {   
            GTDEBUG_COOR("Reload config successfully!\n");
        }
        goto XFER_ERROR;
    }

    finger = (u8)touch_num(touch_data[2]&0x1f, MAX_FINGER_NUM);

/**/GTDEBUG_COOR("touch num:%x\n", finger);

    for (i = 0;i < MAX_FINGER_NUM; i++)        
    {
        finger_current[i] = !!(touch_data[2] & (0x01<<(i)));
    }


    //����У���    
    coor_point = &touch_data[4];
    chk_sum = 0;
    for ( i = 0; i < 5*finger; i++)
    {
        chk_sum += coor_point[i];
/**/ //  GTDEBUG_COOR("%5x", coor_point[i]);
    }
/**///GTDEBUG_COOR("\ncheck sum:%x\n", chk_sum);
/**///GTDEBUG_COOR("check sum byte:%x\n", coor_point[5*finger]);
    if (chk_sum != coor_point[5*finger])
    {
        goto XFER_ERROR;
    }

    //�������//
    if (finger)
    {
        for(i = 0, position=0;position < MAX_FINGER_NUM; position++)
        {  
            if(finger_current[position])
            {     
                X_value = (coor_point[i] << 8) | coor_point[i + 1];
                Y_value = (coor_point[i + 2] << 8) | coor_point[i + 3];

		input_mt_slot(ts->input_dev, position);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, FLAG_DOWN);
				
              //  input_report_key(ts->input_dev, BTN_TOUCH, 1);
              //  input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, position - 1);
              	if(pdata->swap_xy){
					value_tmp = X_value;
					X_value = Y_value;
					Y_value = value_tmp;
				}
				//printk("goodix_touch_82x X_value=%d,Y_value=%d\n",X_value,Y_value);
              	if(pdata->xpol)
					X_value = pdata->xmax - X_value;
				if(pdata->ypol)
					Y_value = pdata->ymax - Y_value;
				
                input_report_abs(ts->input_dev, ABS_MT_POSITION_X, X_value);  //can change x-y!!!
                input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, Y_value);
              //  input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR,15);
             //   input_mt_sync(ts->input_dev);
                i += 5;

  //  /**/        GTDEBUG_COOR("FI:%x X:%04d Y:%04d\n",position, (s32)Y_value,(s32)X_value);
//    printk("-----------touch_num:%d;--------FI:%x----------- X:%04d ----------Y:%04d\n",finger,position, (s32)X_value,(s32)Y_value);
    /**/    //    GTDEBUG_COOR("Y:%d\n", (s32)Y_value);
            }
	  else if(finger_last[position])
	  	{
                  input_mt_slot(ts->input_dev, position);
		  input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
		  GTDEBUG_COOR(" Finger  %d  release!!\n",position);
	       }
        }
    }
    else
    {
              for(position=0;position < MAX_FINGER_NUM; position++)
              	{
              	if( finger_last[position])
              	   {
			input_mt_slot(ts->input_dev, position);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
			 GTDEBUG_COOR(" Finger  %d  release!!\n",position);
              	    }
              }
  }

    input_sync(ts->input_dev);
    
    for(position=0;position<MAX_FINGER_NUM; position++)
    {
        finger_last[position] = finger_current[position];
    }

DATA_NO_READY:
XFER_ERROR:
    if(ts->use_irq && ts->irq_is_disable == 1)
    {
        ts->irq_is_disable = 0;
        enable_irq(ts->client->irq);
    }
}

/*******************************************************	
���ܣ�
	��ʱ����Ӧ����
	�ɼ�ʱ�����������ȴ����������������У�֮�����¼�ʱ
����
	timer����������ļ�ʱ��	
return��
	��ʱ������ģʽ��HRTIMER_NORESTART��ʾ����Ҫ�Զ�����
********************************************************/
static enum hrtimer_restart goodix_ts_timer_func(struct hrtimer *timer)
{
    struct goodix_ts_data *ts = container_of(timer, struct goodix_ts_data, timer);

    queue_work(goodix_wq, &ts->work);
    hrtimer_start(&ts->timer, ktime_set(0, (POLL_TIME+6)*1000000), HRTIMER_MODE_REL);

    return HRTIMER_NORESTART;
}

/*******************************************************	
���ܣ�
	�ж���Ӧ����
	���жϴ��������ȴ��������?������
����
return��
    IRQ_HANDLED:interrupt was handled by this device
********************************************************/
static irqreturn_t goodix_ts_irq_handler(s32 irq, void *dev_id)
{
    struct goodix_ts_data *ts = (struct goodix_ts_data*)dev_id;
	
    if (ts->use_irq && (!ts->irq_is_disable))
    {
        disable_irq_nosync(ts->client->irq);
        ts->irq_is_disable = 1;
    }
    
    queue_work(goodix_wq, &ts->work);

    return IRQ_HANDLED;
}

/*******************************************************	
���ܣ�
	�����Դ������IC����˯�߻��份��
����
	on:	0��ʾʹ��˯�ߣ�1Ϊ����
return��
	�Ƿ����óɹ���successΪ�ɹ�
	�����룺-1Ϊi2c����-2ΪGPIO����-EINVALΪ����on����
********************************************************/
//#if defined(INT_PORT)    // 0 : sleep  1 wake up
static s32 goodix_ts_power(struct goodix_ts_data * ts, s32 on)
{
    s32 ret = -1;

    u8 i2c_control_buf[3] = {0x0f,0xf2,0xc0};        //suspend cmd

    if(ts == NULL || !ts->use_irq)
        return -2;

    switch(on)
    {
    case 0:
        ret = i2c_write_bytes(ts->client, i2c_control_buf, 3);
        return ret;

    case 1:
        GPIO_DIRECTION_OUTPUT(ts->interrupt_port, 0);
        mdelay(10);
        GPIO_SET_VALUE(ts->interrupt_port, 1);
        GPIO_DIRECTION_INPUT(ts->interrupt_port);
        GPIO_PULL_UPDOWN(ts->interrupt_port, 0);

        return success;

    default:
        GTDEBUG_MSG(KERN_DEBUG "%s: Cant't support this command.", goodix_ts_name);
        return -EINVAL;
    }

}

static s32 init_input_dev(struct goodix_ts_data *ts)
{
    s32 i;
    s32 ret = 0;

    ts->input_dev = input_allocate_device();
    if (ts->input_dev == NULL)
    {
        dev_dbg(&ts->client->dev,"goodix_ts_probe: Failed to allocate input device\n");
        return fail;
    }
 
//    ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
//    ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
 //   ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);// absolute coor (x,y)


    goodix_init_panel(ts, 0);
 
#ifdef GOODIX_MULTI_TOUCH

	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	__set_bit(EV_ABS, ts->input_dev->evbit);
	input_mt_init_slots(ts->input_dev, MAX_FINGER_NUM);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, TOUCH_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, TOUCH_MAX_WIDTH, 0, 0);

  /* input_mt_init_slots(ts->input_dev, MAX_FINGER_NUM);
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, TOUCH_MAX_HEIGHT, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, TOUCH_MAX_WIDTH, 0, 0);
    */
#else
    input_set_abs_params(ts->input_dev, ABS_X, 0, TOUCH_MAX_HEIGHT, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_Y, 0, TOUCH_MAX_WIDTH, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
#endif    

    memcpy(ts->phys, "input/ts", 8);
    ts->input_dev->name = goodix_ts_name;
    ts->input_dev->phys = ts->phys;
    ts->input_dev->id.bustype = BUS_I2C;
    ts->input_dev->id.vendor = 0xDEAD;
    ts->input_dev->id.product = 0xBEEF;
    ts->input_dev->id.version = 10427;    //screen firmware version

#ifdef LONGPRESS_LOCK_SPECKEY    //w++
    set_bit(KEY_LOCK, ts->input_dev->keybit);
#endif
    ret = input_register_device(ts->input_dev);
    if (ret) 
    {
        dev_err(&ts->client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
        input_free_device(ts->input_dev);
        return fail;
    }
    GTDEBUG_MSG("Register input device successfully!\n");

    return success;
}

static s32 set_pins(struct goodix_ts_data *ts)
{
    s32 ret = -1;
    
  //  ts->client->irq=TS_INT;        //If not defined in client
    if (ts->client->irq)
    {
        ret = GPIO_REQUEST(ts->interrupt_port, "TS_INT");    //Request IO
        if (ret < 0) 
        {
            dev_err(&ts->client->dev, "Failed to request GPIO:%d, ERRNO:%d\n",(s32)ts->interrupt_port,ret);
            goto err_gpio_request_failed;
        }
        GTDEBUG_MSG("Request int port successfully!\n");
        
        GPIO_DIRECTION_INPUT(ts->interrupt_port);
        GPIO_PULL_UPDOWN(ts->interrupt_port, 0);
//        GPIO_CFG_PIN(INT_PORT, INT_CFG);        //Set IO port function    


ret  = request_irq(gpio_to_irq(ts->interrupt_port), goodix_ts_irq_handler,
				IRQF_TRIGGER_FALLING, ts->client->name, ts);

     //   ret = request_irq(ts->client->irq, goodix_ts_irq_handler, INT_TRIGGER,    ts->client->name, ts);
                      
        if (ret != 0) 
        {
            dev_err(&ts->client->dev,"Cannot allocate ts INT!ERRNO:%d\n", ret);
            GPIO_DIRECTION_INPUT(ts->interrupt_port);
            GPIO_FREE(ts->interrupt_port);
            goto err_gpio_request_failed;
        }
        else 
        {
            disable_irq(ts->client->irq);
            ts->use_irq = 1;
            ts->irq_is_disable = 1;
            dev_dbg(&ts->client->dev, "Reques EIRQ %d successed on GPIO:%d\n", gpio_to_irq(ts->interrupt_port), ts->interrupt_port);
        }
    }

err_gpio_request_failed:
    if (!ts->use_irq) 
    {
        hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        ts->timer.function = goodix_ts_timer_func;
        hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
        GTDEBUG_MSG("Use timer!\n");
    }

    ret = GPIO_REQUEST(ts->reset_port, "TS_RESET");    //Request IO
    if (ret < 0) 
    {
        dev_err(&ts->client->dev, "Failed to request GPIO:%d, ERRNO:%d\n",(s32)ts->reset_port,ret);
    }
    else
    {
        ts->use_reset = 1;
        GPIO_DIRECTION_OUTPUT(ts->reset_port,1);
        GPIO_PULL_UPDOWN(ts->reset_port, 0);
	GTDEBUG_MSG("Request reset port successfully!\n");	
    }

    dev_info(&ts->client->dev,"Start %s in %s mode\n", 
              ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");

    return ts->use_irq;
}

/*******************************************************	
���ܣ�
	������̽�⺯��
	��ע����ʱ����(Ҫ����ڶ�Ӧ��client)��
	����IO,�жϵ���Դ���룻�豸ע�᣻��������ʼ���ȹ���
����
	client��������豸�ṹ��
	id���豸ID
return��
	ִ�н���룬0��ʾ��ִ��
********************************************************/
static s32 goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    s32 ret = 0;
    s32 retry=0;
    struct goodix_ts_data *ts = NULL;
    struct goodix_i2c_rmi_platform_data *pdata = client->dev.platform_data;
		//struct ft5406_platform_data *pdata = pdata = client->dev.platform_data;
    printk("w++++++goodix_ts_probe gt28x ");

	if (pdata->init_platform_hw)
	{
		pdata->init_platform_hw();
	}

//    GTDEBUG("Start to install Goodix Capacitive TouchScreen driver.\n");
//    GTDEBUG("*DRIVER INFORMATION\n");
//    GTDEBUG("**RELEASE DATE:%s.\n", RELEASE_DATE);
//    GTDEBUG("**COMPILE TIME:%s, %s.\n", __DATE__, __TIME__);
//    printk("gt82x---0000000000000000\n");
    //Check I2C function
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
    {
        dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
        return -ENODEV;
    }

    ts = kzalloc(sizeof(*ts), GFP_KERNEL);
    if (ts == NULL)
    {
        return -ENOMEM;
    }
      pdata = client->dev.platform_data;

    INIT_WORK(&ts->work, goodix_ts_work_func);        //init work_struct
    ts->client = client;
    ts->power = goodix_ts_power;
    ts->bad_data = 0;
    ts->use_irq = 1;
    ts->use_reset =1;
    ts->irq_is_disable = 0;
    ts->interrupt_port=pdata->gpio_irq;
    ts->reset_port=pdata->gpio_reset;
    i2c_set_clientdata(client, ts);
 
    if (fail == init_input_dev(ts))
    {   kfree(ts);
        return -1;
    }
    set_pins(ts);
#ifdef CONFIG_HAS_EARLYSUSPEND
    ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    ts->early_suspend.suspend = goodix_ts_early_suspend;
    ts->early_suspend.resume = goodix_ts_late_resume;
    register_early_suspend(&ts->early_suspend);
#endif
#ifdef CREATE_WR_NODE
    init_wr_node(client);
#endif
#ifdef AUTO_UPDATE_GUITAR
    if (0xff == init_update_proc(ts))
    {
        GTDEBUG_MSG("Need update!\n");
        return 0;
    }
#else
    msleep(5);
    guitar_reset(ts,10);
#endif

    //Test I2C connection. 
//    GTDEBUG_MSG("GT82X++++++   Testing I2C connection...\n");
    for(retry = 0;retry < 3; retry++)
    while(1)            //For GTDEBUG use!
    {
        ret = i2c_pre_cmd(ts);
        if (ret > 0)
            break;
        msleep(20);
    }
    if(ret <= 0)
    {
        dev_err(&client->dev, "Warnning: I2C communication might be ERROR!\n");
        GTDEBUG_MSG("I2C test failed. I2C addr:%x\n", client->addr);
        goodix_ts_remove(ts->client);  kfree(ts);
        return -1;
    }
//    printk("gt82x---777777777777777\n");
    //Send config
    for (retry = 0; retry < 3; retry++)
    {
        if (success == goodix_init_panel(ts, 1))
        {
            GTDEBUG_MSG("Initialize successfully!\n");
            break;
        }
    }
    if (retry >= 3)
    {
        ts->bad_data=1;
        GTDEBUG_MSG("Initialize failed!\n");
        goodix_ts_remove(ts->client);  kfree(ts);
        return -1;
    }
    //Enable interrupt
    if(ts->use_irq && ts->irq_is_disable == 1)
    {
        ts->irq_is_disable = 0;
        enable_irq(client->irq);
    }

    return 0;
}


/*******************************************************	
���ܣ�
	����Դ�ͷ�
����
	client���豸�ṹ��
return��
	ִ�н���룬success��ʾ��ִ��
********************************************************/
static s32 goodix_ts_remove(struct i2c_client *client)
{
    struct goodix_ts_data *ts = i2c_get_clientdata(client);

    dev_notice(&client->dev,"The driver is removing...\n");
    
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ts->early_suspend);
#endif

#ifdef CREATE_WR_NODE
    uninit_wr_node();
#endif

    if (ts && ts->use_irq) 
    {
        free_irq(client->irq, ts);
        GPIO_DIRECTION_INPUT(ts->interrupt_port);
        GPIO_FREE(ts->interrupt_port);
    }
    else if(ts)
        hrtimer_cancel(&ts->timer);

    if (ts && ts->use_reset)
    {
        GPIO_DIRECTION_INPUT(ts->reset_port);
        GPIO_FREE(ts->interrupt_port);
    }
    
    i2c_set_clientdata(client, NULL);
    input_unregister_device(ts->input_dev);
    input_free_device(ts->input_dev);
    kfree(ts);
    return success;
}

//ͣ���豸
static s32 goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
    s32 ret;
    struct goodix_ts_data *ts = i2c_get_clientdata(client);

    if (ts->irq_is_disable == 2)
    {
        return 0;
    }

    if (ts->use_irq)
    {
        if (!ts->irq_is_disable)
        {
            disable_irq(client->irq);
            ts->irq_is_disable = 1;
        }
    }
    else
    {
        hrtimer_cancel(&ts->timer);
    }
    
    if (ts->power) 
    {
        ret = ts->power(ts, 0);
        if (ret <= 0)
            GTDEBUG_MSG(KERN_ERR "goodix_ts_resume power off failed\n");
    }
		printk("-----gt813------suspend.\n");
    return 0;
}

static s32 goodix_ts_resume(struct i2c_client *client)
{
    s32 ret;
    struct goodix_ts_data *ts = i2c_get_clientdata(client);

    if (ts->irq_is_disable == 2)
    {
        return 0;
    }

    if (ts->power) 
    {
        ret = ts->power(ts, 1);
        if (ret <= 0)
            GTDEBUG_MSG(KERN_ERR "goodix_ts_resume power on failed\n");
    }

    if (ts->use_irq)
    {
        ts->irq_is_disable = 0;
        enable_irq(client->irq);
    }
    else
    {
        hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
    }
		printk("-----gt813------goodix_ts_resume.\n");

    return success;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h)
{
    struct goodix_ts_data *ts;
    ts = container_of(h, struct goodix_ts_data, early_suspend);
    goodix_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void goodix_ts_late_resume(struct early_suspend *h)
{
    struct goodix_ts_data *ts;
    ts = container_of(h, struct goodix_ts_data, early_suspend);
    goodix_ts_resume(ts->client);
}
#endif
//******************************Begin of firmware update surpport*******************************

#ifndef AUTO_UPDATE_GUITAR
static void guitar_reset( struct goodix_ts_data *ts,u8 ms)
{
   
    GPIO_DIRECTION_OUTPUT(ts->reset_port, 0);
    GPIO_SET_VALUE(ts->reset_port, 0);
    msleep(ms);

   // GPIO_DIRECTION_OUTPUT(ts->reset_port);
    GPIO_SET_VALUE(ts->reset_port, 1);
    GPIO_PULL_UPDOWN(ts->reset_port, 0);

    msleep(20);
err_gpio_request_failed:	
    return;
}
#endif


//�����ڸ���� �豸���豸ID �б�
//only one client
static const struct i2c_device_id goodix_ts_id[] = {
    {"Goodix-TS-82X", 0 },
    { }
};

//�豸��ṹ��
static struct i2c_driver goodix_ts_driver = {
    .probe      = goodix_ts_probe,
    .remove     = goodix_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
    .suspend    = goodix_ts_suspend,
    .resume     = goodix_ts_resume,
#endif
    .id_table   = goodix_ts_id,
    .driver     = {
        .name   =  "Goodix-TS-82X",
        .owner  = THIS_MODULE,
    },
};
/*******************************************************	
���ܣ�
	����غ���
return��
	ִ�н���룬0��ʾ��ִ��
********************************************************/
static s32 __devinit goodix_ts_init(void)
{
	int ret; 
		printk("+++++++++++++++++++++++++++++liqing.\n");
    goodix_wq = create_workqueue("goodix_wq");        //create a work queue and worker thread
    if (!goodix_wq)
    {
        GTDEBUG_MSG(KERN_ALERT "creat workqueue faiked\n");
        return -ENOMEM;
    }
    ret = i2c_add_driver(&goodix_ts_driver);
	if (ret)
	{
	    printk("Register raydium_ts driver failed gt82x.\n");
    		return ret;   //w++
	}
#ifdef LONGPRESS_LOCK_SPECKEY
	if(err_gt82x>=0)   //w++

	ret =driver_create_file(&goodix_ts_driver.driver, &driver_attr_get_lock_status);
#endif
    return ret;
}

/*******************************************************	
���ܣ�
	��ж�غ���
����
	client���豸�ṹ��
********************************************************/
static void __exit goodix_ts_exit(void)
{
    GTDEBUG_MSG(KERN_ALERT "Touchscreen driver of guitar exited.\n");
    i2c_del_driver(&goodix_ts_driver);
    if (goodix_wq)
        destroy_workqueue(goodix_wq);        //release our work queue
#ifdef LONGPRESS_LOCK_SPECKEY   //w++
	driver_remove_file(&goodix_ts_driver.driver, &driver_attr_get_lock_status);
#endif
}

module_init(goodix_ts_init);                //����ʼ����
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");

