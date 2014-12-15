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
#include <linux/err.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include "gt82x.h"

#define READ_TOUCH_ADDR_H   0x0F
#define READ_TOUCH_ADDR_L   0x40
#define READ_KEY_ADDR_H     0x0F
#define READ_KEY_ADDR_L     0x41
#define READ_COOR_ADDR_H    0x0F
#define READ_COOR_ADDR_L    0x42
#define RESOLUTION_LOC      71
#define TRIGGER_LOC         66

#define GOODIX_I2C_NAME "Goodix-TS"

static struct workqueue_struct *goodix_wq;

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
#endif

#define _ENABLE_DBG_LEVEL
#ifdef _ENABLE_DBG_LEVEL
#define DBG_INT    (1<<0)
#define DBG_INFO   (1<<1)
#define DBG_DATA   (1<<2)
#define DBG_REPORT (1<<3)
static int dbg_level = DBG_INFO;
#define goodix_dbg(level, fmt, args...)  { if( (level&dbg_level)>0 ) \
					printk("[goodix]: " fmt, ## args); }

#define PROC_FS_NAME	"gt82x_dbg"
#define PROC_FS_MAX_LEN	8
static struct proc_dir_entry *dbgProcFile;

static int gt82x_proc_read(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data )
{
    return sprintf(buffer, "Debug Level: 0x%08X\n", dbg_level);

}

static int gt82x_proc_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	char procfs_buffer_size = 0; 
	int i;
	unsigned char procfs_buf[PROC_FS_MAX_LEN+1] = {0};


	procfs_buffer_size = count;
	if(procfs_buffer_size > PROC_FS_MAX_LEN ) 
		procfs_buffer_size = PROC_FS_MAX_LEN+1;
	
	if( copy_from_user(procfs_buf, buffer, procfs_buffer_size) ) 
	{
		printk(" proc_write faied at copy_from_user\n");
		return -EFAULT;
	}

	if (sscanf(procfs_buf, "%x", &i) == 1) {
	  dbg_level = i&0xFFFFFFFF;
    printk(" Switch Debug Level to 0x%08X\n", dbg_level);
  }
  else {
    printk(" Bad Debug Level!\n");
  }

	return count;
}
#endif

/*******************************************************	
功能：	
	读取从机数据
	每个读操作用两条i2c_msg组成，第1条消息用于发送从机地址，
	第2条用于发送读取地址和取回数据；每条消息前发送起始信号
参数：
	client:	i2c设备，包含设备地址
	buf[0]~buf[1]：	 首字节为读取地址
	buf[2]~buf[len]：数据缓冲区
	len：	读取数据长度
return：
	执行消息数
*********************************************************/
/*Function as i2c_master_send */
static s32 i2c_read_bytes(struct i2c_client *client, u8 *buf, s32 len)
{
    struct i2c_msg msgs[2];
    s32 ret=-1;

    //发送写地址
    msgs[0].flags=!I2C_M_RD; //写消息
    msgs[0].addr=client->addr;
    msgs[0].len=2;
    msgs[0].buf=&buf[0];
    //接收数据
    msgs[1].flags=I2C_M_RD;//读消息
    msgs[1].addr=client->addr;
    msgs[1].len=len - ADDR_LENGTH;
    msgs[1].buf=&buf[2];

    ret=i2c_transfer(client->adapter,msgs, 2);

    return ret;
}

/*******************************************************	
功能：
	向从机写数据
参数：
	client:	i2c设备，包含设备地址
	buf[0]~buf[1]：	 首字节为写地址
	buf[2]~buf[len]：数据缓冲区
	len：	数据长度	
return：
	执行消息数
*******************************************************/
/*Function as i2c_master_send */
static s32 i2c_write_bytes(struct i2c_client *client,u8 *data,s32 len)
{
    struct i2c_msg msg;
    s32 ret=-1;
    
    //发送设备地址
    msg.flags=!I2C_M_RD;//写消息
    msg.addr=client->addr;
    msg.len=len;
    msg.buf=data;        

    ret=i2c_transfer(client->adapter,&msg, 1);

    return ret;
}

/*******************************************************
功能：
	发送前缀命令
	
	ts:	client私有数据结构体
return：

	执行结果码，0表示正常执行
*******************************************************/
static s32 i2c_pre_cmd(struct goodix_ts_data *ts)
{
    s32 ret;
    u8 pre_cmd_data[2]={0x0f, 0xff};

    ret=i2c_write_bytes(ts->client,pre_cmd_data,2);
    return ret;//*/
}

/*******************************************************
功能：
	发送后缀命令
	
	ts:	client私有数据结构体
return：

	执行结果码，0表示正常执行
*******************************************************/
static s32 i2c_end_cmd(struct goodix_ts_data *ts)
{
    s32 ret;
    u8 end_cmd_data[2]={0x80, 0x00};    

    ret=i2c_write_bytes(ts->client,end_cmd_data,2);
    return ret;//*/
}

/*******************************************************
功能：
	Guitar初始化函数，用于发送配置信息，获取版本信息
参数：
	ts:	client私有数据结构体
return：
	执行结果码，0表示正常执行
*******************************************************/
s32 goodix_init_panel(struct goodix_ts_data *ts)
{
    s32 ret;
    u8 *config = (u8 *)ts->pdata->data;

    ret = i2c_write_bytes(ts->client, config, ts->pdata->data_len);
    if (ret <= 0)
    {
        dev_err(&(ts->client->dev),"init panel failed(i2c error %d)\n ", ret);
        return fail;
    }
    else
    {
        dev_info(&(ts->client->dev),"init panel success\n");
        i2c_end_cmd(ts);
        msleep(500);
    }
    return success;
}

/*******************************************************	
功能：
	触摸屏工作函数
	由中断触发，接受1组坐标数据，校验后再分析输出
参数：
	ts:	client私有数据结构体
return：
	执行结果码，0表示正常执行
********************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{
    u8 finger, key, key_change;
    u8 chk_sum = 0;
    u16 x, y;
    s32 i, ret;
    u8 touch_data[2 + 2 + 5*MAX_FINGER_NUM + 1] = {READ_TOUCH_ADDR_H,READ_TOUCH_ADDR_L,0, 0};
    u8 *p;

    struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);

    ret=i2c_read_bytes(ts->client, touch_data, ARRAY_SIZE(touch_data)); 
    i2c_end_cmd(ts);
    finger = touch_data[2] & 0x1f;
    key = touch_data[3] & 0x0f;
    
    if(ret <= 0) {
        goodix_dbg(DBG_INFO,"I2C transfer error. Number:%d\n ", ret);
        goto XFER_ERROR;
    }
    else if((touch_data[2]&0xC0)!=0x80) {
    		goodix_dbg(DBG_INFO, "data not ready, may be caused by inormal reset\n");
        goto XFER_ERROR;
    }
    else if (key == 0x0f) {
    		goodix_dbg(DBG_INFO, "unknown error, pls calibrate again\n");
        goto XFER_ERROR;
    }
    
    goodix_dbg(DBG_DATA, "touch data:%5x%5x\n", touch_data[2], touch_data[3]);
    p = &touch_data[4];
    for (i=0; i<MAX_FINGER_NUM; i++) {
        if((finger>>i) & 1) {
            goodix_dbg(DBG_DATA, "%5x%5x%5x%5x%5x\n", *p, *(p+1), *(p+2), *(p+3), *(p+4));
            chk_sum += *p + *(p+1) + *(p+2) + *(p+3) + *(p+4);
            p += 5;
        }
    }
    if (chk_sum != *p) {
        goodix_dbg(DBG_DATA, "check sum error(%d, %d)\n", *p, chk_sum);
        goto XFER_ERROR;
    }
 
    if (finger) {
        p = &touch_data[4];
        for(i=0; i<MAX_FINGER_NUM; i++) {
            if((finger>>i) & 1) {
                x = (*p << 8) | *(p+1);
                y = (*(p+2) << 8) | *(p+3);
                if (ts->pdata->swap_xy) swap(x, y);
                if (ts->pdata->xpol) x = ts->pdata->xmax+ ts->pdata->xmin - x;
                if (ts->pdata->ypol) y = ts->pdata->ymax+ ts->pdata->ymin - x;
                input_report_key(ts->input_dev, BTN_TOUCH, 1);
                input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
                input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
                input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
                input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR,15);
                input_mt_sync(ts->input_dev);
                p += 5;
            		goodix_dbg(DBG_REPORT, "point[%d]=(%d, %d)\n", i, x, y);
            }
        }
    }
    else {
        input_report_key(ts->input_dev, BTN_TOUCH, 0);
        input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
        input_mt_sync(ts->input_dev);
        goodix_dbg(DBG_REPORT, "fingers up!\n\n");
    }

    key_change = key ^ ts->last_key;
    if (ts->pdata->key_list && key_change) {
        for(i=0; i<ts->pdata->key_num; i++) {
            if ((key_change>>i)&1) {
                input_report_key(ts->input_dev, ts->pdata->key_list[i].value, (key>>i)&1);
                goodix_dbg(DBG_REPORT, "key %d %s\n", ts->pdata->key_list[i].value, (key>>i)&1 ? "down":"up");
            }
        }
        ts->last_key = key;
    }
    input_sync(ts->input_dev);
    
XFER_ERROR:
    if(ts->irq_is_disable == 1)
    {
        ts->irq_is_disable = 0;
        enable_irq(ts->client->irq);
    }
}

/*******************************************************	
功能：
	中断响应函数
	由中断触发，调度触摸屏处理函数运行
参数：
	timer：函数关联的计时器	
return：
	计时器工作模式，HRTIMER_NORESTART表示不需要自动重启
********************************************************/
static irqreturn_t goodix_ts_irq_handler(s32 irq, void *dev_id)
{
    struct goodix_ts_data *ts = (struct goodix_ts_data*)dev_id;
		static int irq_count = 0;

		goodix_dbg(DBG_INT, "irq_count=%d\n", irq_count++);
    if (!ts->irq_is_disable)
    {
        disable_irq_nosync(ts->client->irq);
        ts->irq_is_disable = 1;
        queue_work(goodix_wq, &ts->work);
    }
        
    return IRQ_HANDLED;
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

    ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
    ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
    ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);// absolute coor (x,y)
    
    if (ts->pdata->key_list)
        for(i = 0; i < ts->pdata->key_num; i++)
            input_set_capability(ts->input_dev, EV_KEY, ts->pdata->key_list[i].value);
    
#ifdef GOODIX_MULTI_TOUCH
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->pdata->xmax, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->pdata->ymax, 0, 0);
#else
    input_set_abs_params(ts->input_dev, ABS_X, 0, ts->pdata->xmax, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_Y, 0, ts->pdata->ymax, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
#endif    

    memcpy(ts->phys, "input/ts", 8);
    ts->input_dev->name = GOODIX_I2C_NAME;
    ts->input_dev->phys = ts->phys;
    ts->input_dev->id.bustype = BUS_I2C;
    ts->input_dev->id.vendor = 0xDEAD;
    ts->input_dev->id.product = 0xBEEF;
    ts->input_dev->id.version = 10427;    //screen firmware version

    ret = input_register_device(ts->input_dev);
    if (ret) 
    {
        dev_err(&ts->client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
        input_free_device(ts->input_dev);
        return fail;
    }
    DEBUG_MSG("Register input device successfully!\n");

    return success;
}

/*******************************************************	
功能：
	触摸屏探测函数
	在注册驱动时调用(要求存在对应的client)；
	用于IO,中断等资源申请；设备注册；触摸屏初始化等工作
参数：
	client：待驱动的设备结构体
	id：设备ID
return：
	执行结果码，0表示正常执行
********************************************************/
static s32 goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    s32 ret = 0;
    s32 retry=0;
    struct goodix_ts_data *ts = NULL;
    struct ctp_platform_data *pdata = (struct ctp_platform_data *)client->dev.platform_data;
    
    dev_dbg(&client->dev,"Install touch driver.\n");

    if (!pdata) {
        dev_err(&client->dev, "No platform data, Pls add platform data in bsp!\n");
        return -ENODEV;
    }
		
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

    INIT_WORK(&ts->work, goodix_ts_work_func);        //init work_struct
    ts->client = client;
    ts->pdata = pdata;
    i2c_set_clientdata(client, ts);

    if (fail == init_input_dev(ts))
    {
        return -1;
    }
    
    goodix_ts_power(ts, 1);
    msleep(10);
		guitar_reset(ts, 50);
    
    client->irq = pdata->irq;
    disable_irq_nosync(client->irq);
    ts->irq_is_disable = 1;
    if (pdata->init_irq) {
        pdata->init_irq();
    }  
    ret = request_irq(ts->client->irq, goodix_ts_irq_handler, pdata->irq_flag,
            ts->client->name, ts);
    if (ret != 0) 
    {
        DEBUG_MSG("Cannot allocate ts INT(%d)! ERRNO:%d\n", ts->client->irq, ret);
        return -1;
    }
    else 
    {
        DEBUG_MSG("Reques EIRQ %d successed\n", ts->client->irq);
    }

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
        DEBUG_MSG("Need update!\n");
        return 0;
    }
#endif

    //Test I2C connection.    
    DEBUG_MSG("Testing I2C connection...\n");
    for(retry = 0;retry < 3; retry++)
    //while(1)            //For debug use!
    {
    		printk("retry pre_cmd %d\n", retry);
        ret = i2c_pre_cmd(ts);
        if (ret > 0)
            break;
        msleep(20);
    }
    if(ret <= 0)
    {
        dev_err(&client->dev, "Warnning: I2C communication might be ERROR!\n");
        DEBUG_MSG("I2C test failed. I2C addr:%x\n", client->addr);
        //goodix_ts_remove(ts->client);
        return -1;
    }

    //Send config
    for (retry = 0; retry < 3; retry++)
    {
        if (success == goodix_init_panel(ts))
        {
            DEBUG_MSG("Initialize successfully!\n");
            break;
        }
    }
    if (retry >= 3)
    {
        DEBUG_MSG("Initialize failed!\n");
        goodix_ts_remove(ts->client);
        return -1;
    }

    //Enable interrupt
    if(ts->irq_is_disable == 1)
    {
    		DEBUG_MSG("gt827 proble finished and enable interrupt!\n");
        ts->irq_is_disable = 0;
        //enable_irq(client->irq);
    }

    return 0;
}


/*******************************************************	
功能：
	驱动资源释放
参数：
	client：设备结构体
return：
	执行结果码，success表示正常执行
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

    free_irq(client->irq, ts);    
    i2c_set_clientdata(client, NULL);
    input_unregister_device(ts->input_dev);
    input_free_device(ts->input_dev);
    kfree(ts);
    return success;
}

//停用设备
static s32 goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
    struct goodix_ts_data *ts = i2c_get_clientdata(client);

    if (!ts->irq_is_disable)
    {
        disable_irq(client->irq);
        ts->irq_is_disable = 1;
    }

    goodix_ts_power(ts, 0);
    return 0;
}

static s32 goodix_ts_resume(struct i2c_client *client)
{
    struct goodix_ts_data *ts = i2c_get_clientdata(client);
    
    goodix_ts_power(ts, 1);
    guitar_reset(ts, 50);
		goodix_init_panel(ts);
    
    ts->irq_is_disable = 0;
    enable_irq(client->irq);
    
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

//可用于该驱动的 设备名—设备ID 列表
//only one client
static const struct i2c_device_id goodix_ts_id[] = {
    { GOODIX_I2C_NAME, 0 },
    { }
};

//设备驱动结构体
static struct i2c_driver goodix_ts_driver = {
    .probe      = goodix_ts_probe,
    .remove     = goodix_ts_remove,
//#ifndef CONFIG_HAS_EARLYSUSPEND
//    .suspend    = goodix_ts_suspend,
//    .resume     = goodix_ts_resume,
//#endif
    .id_table   = goodix_ts_id,
    .driver     = {
        .name   = GOODIX_I2C_NAME,
        .owner  = THIS_MODULE,
    },
};

/*******************************************************	
功能：
	驱动加载函数
return：
	执行结果码，0表示正常执行
********************************************************/
static s32 __devinit goodix_ts_init(void)
{
    goodix_wq = create_workqueue("goodix_wq");        //create a work queue and worker thread
    if (!goodix_wq)
    {
        DEBUG_MSG(KERN_ALERT "creat workqueue faiked\n");
        return -ENOMEM;
    }
#ifdef _ENABLE_DBG_LEVEL
    dbgProcFile = create_proc_entry(PROC_FS_NAME, 0666, NULL);
    if (dbgProcFile == NULL) {
        remove_proc_entry(PROC_FS_NAME, NULL);
        DEBUG_MSG(KERN_ALERT, " Could not initialize /proc/%s\n", PROC_FS_NAME);
    }
    else {
        dbgProcFile->read_proc = gt82x_proc_read;
        dbgProcFile->write_proc = gt82x_proc_write;
        DEBUG_MSG(KERN_ALERT" /proc/%s created\n", PROC_FS_NAME);
    }
#endif // #ifdef _ENABLE_DBG_LEVEL    
    return i2c_add_driver(&goodix_ts_driver);
}

/*******************************************************	
功能：
	驱动卸载函数
参数：
	client：设备结构体
********************************************************/
static void __exit goodix_ts_exit(void)
{
    DEBUG_MSG(KERN_ALERT "Touchscreen driver of guitar exited.\n");
    i2c_del_driver(&goodix_ts_driver);
    if (goodix_wq)
        destroy_workqueue(goodix_wq);        //release our work queue
#ifdef _ENABLE_DBG_LEVEL
    remove_proc_entry(PROC_FS_NAME, NULL);
#endif
}
#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_late_init(goodix_ts_init);
#else
late_initcall(goodix_ts_init);                //最后初始化驱动
#endif
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");


