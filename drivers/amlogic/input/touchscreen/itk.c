/*
 * linux/drivers/input/touchscreen/itk.c
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Written by x
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/i2c/itk.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend itk_early_suspend;
#endif

// definition
#define ILITEK_I2C_RETRY_COUNT			3
#define ILITEK_I2C_DEFAULT_ADDRESS      0x41
#define ILITEK_I2C_DRIVER_NAME			"ilitek_i2c"
#define ILITEK_FILE_DRIVER_NAME         "ilitek_file"
#define ILITEK_DEBUG_LEVEL			KERN_INFO
#define ILITEK_ERROR_LEVEL			KERN_ALERT

// i2c command for ilitek touch screen
#define ILITEK_TP_CMD_READ_DATA         0x10
#define ILITEK_TP_CMD_GET_RESOLUTION    0x20
#define ILITEK_TP_CMD_SET_SLEEP_MODE		0x30
#define ILITEK_TP_CMD_GET_VERSION       0x40
#define ILITEK_TP_CMD_CALIBRATION       0xCC
#define ILITEK_TP_CMD_ERASE_BACKGROUND		  0xCE

// define the application command
#define ILITEK_IOCTL_BASE                       100
#define ILITEK_IOCTL_I2C_WRITE_DATA             _IOWR(ILITEK_IOCTL_BASE, 0, unsigned char*)
#define ILITEK_IOCTL_I2C_WRITE_LENGTH           _IOWR(ILITEK_IOCTL_BASE, 1, int)
#define ILITEK_IOCTL_I2C_READ_DATA              _IOWR(ILITEK_IOCTL_BASE, 2, unsigned char*)
#define ILITEK_IOCTL_I2C_READ_LENGTH            _IOWR(ILITEK_IOCTL_BASE, 3, int)
#define ILITEK_IOCTL_USB_WRITE_DATA             _IOWR(ILITEK_IOCTL_BASE, 4, unsigned char*)
#define ILITEK_IOCTL_USB_WRITE_LENGTH           _IOWR(ILITEK_IOCTL_BASE, 5, int)
#define ILITEK_IOCTL_USB_READ_DATA              _IOWR(ILITEK_IOCTL_BASE, 6, unsigned char*)
#define ILITEK_IOCTL_USB_READ_LENGTH            _IOWR(ILITEK_IOCTL_BASE, 7, int)
#define ILITEK_IOCTL_I2C_UPDATE_RESOLUTION      _IOWR(ILITEK_IOCTL_BASE, 8, int)
#define ILITEK_IOCTL_USB_UPDATE_RESOLUTION      _IOWR(ILITEK_IOCTL_BASE, 9, int)
#define ILITEK_IOCTL_I2C_SET_ADDRESS            _IOWR(ILITEK_IOCTL_BASE, 10, int)
#define ILITEK_IOCTL_I2C_UPDATE                 _IOWR(ILITEK_IOCTL_BASE, 11, int)
#define ILITEK_IOCTL_STOP_READ_DATA             _IOWR(ILITEK_IOCTL_BASE, 12, int)
#define ILITEK_IOCTL_START_READ_DATA            _IOWR(ILITEK_IOCTL_BASE, 13, int)
static int ilitek_i2c_transfer(struct i2c_client*, struct i2c_msg*, int);
static long ilitek_file_ioctl(struct file*, unsigned int, unsigned long);
static int ilitek_file_open(struct inode*, struct file*);
static ssize_t ilitek_file_write(struct file*, const char*, size_t, loff_t*);
static ssize_t ilitek_file_read(struct file*, char*, size_t, loff_t*);
static int ilitek_file_close(struct inode*, struct file*);
struct i2c_client i2c_dev;
struct semaphore wr_sem;
// declare i2c data member
struct i2c_data {
	// input device
        struct input_dev *input_dev;
        // i2c client
        struct i2c_client *client;
        // polling thread
        struct task_struct *thread;
        // maximum x
        int max_x;
        // maximum y
        int max_y;
	// maximum touch point
	int max_tp;
	// maximum key button
	int max_btn;
        // the total number of x channel
        int x_ch;
        // the total number of y channel
        int y_ch;
        // check whether i2c driver is registered success
        int valid_i2c_register;
        // check whether input driver is registered success
        int valid_input_register;
	// check whether the i2c enter suspend or not
	int stop_polling;
	// read semaphore
	struct semaphore wr_sem;
	// protocol version
	int protocol_ver;
	// valid irq request
	int valid_irq_request;
	// work queue for interrupt use only
	struct workqueue_struct *irq_work_queue;
	// work struct for work queue
	struct work_struct irq_work;
    struct timer_list timer;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
    //struct hrtimer timer;
};
struct dev_data {
        // device number
        dev_t devno;
        // character device
        struct cdev cdev;
        // class device
        struct class *class;
};

// global variables
static struct i2c_data i2c;
static struct dev_data dev;

/*
// file operation functions
static int ilitek_file_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
static int ilitek_file_open(struct inode*, struct file*);
static ssize_t ilitek_file_write(struct file*, const char*, size_t, loff_t*);
static int ilitek_file_close(struct inode*, struct file*);
*/

// declare file operations
struct file_operations ilitek_fops = {
    .unlocked_ioctl = ilitek_file_ioctl,
    .read = ilitek_file_read,
    .write = ilitek_file_write,
    .open = ilitek_file_open,
    .release = ilitek_file_close,
};

static int 
ilitek_file_open(
    struct inode *inode, struct file *filp)
{
    return 0; 
}

static ssize_t 
ilitek_file_write(
	struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	int ret;
	unsigned char buffer[128]={0};
        struct i2c_msg msgs[] = {
		{.addr = i2c.client->addr, .flags = 0, .len = count, .buf = buffer,}
	};
        
	// before sending data to touch device, we need to check whether the device is working or not
//	if(i2c.valid_i2c_register == 0){
//		printk(ILITEK_ERROR_LEVEL "%s, i2c device driver doesn't be registered\n", __func__);
//		return -1;
//	}

	// check the buffer size whether it exceeds the local buffer size or not
	if(count > 128){
		printk(ILITEK_ERROR_LEVEL "%s, buffer exceed 128 bytes\n", __func__);
		return -1;
	}

	// copy data from user space
	ret = copy_from_user(buffer, buf, count-1);
	if(ret < 0){
		printk(ILITEK_ERROR_LEVEL "%s, copy data from user space, failed", __func__);
		return -1;
	}

	// parsing command
        if(strcmp(buffer, "calibrate") == 0){
		buffer[0] = ILITEK_TP_CMD_ERASE_BACKGROUND;
                msgs[0].len = 1;
                ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
                if(ret < 0){
                        printk(ILITEK_DEBUG_LEVEL "%s, i2c erase background, failed\n", __func__);
                }
                else{
                        printk(ILITEK_DEBUG_LEVEL "%s, i2c erase background, success\n", __func__);
                }

		buffer[0] = ILITEK_TP_CMD_CALIBRATION;
                msgs[0].len = 1;
                msleep(2000);
                ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
		if(ret < 0){
                        printk(ILITEK_DEBUG_LEVEL "%s, i2c calibration, failed\n", __func__);
                }
		else{
                	printk(ILITEK_DEBUG_LEVEL "%s, i2c calibration, success\n", __func__);
		}
		msleep(1000);
                return count;
	}
	return -1;
}

static long
ilitek_file_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	static unsigned char buffer[64]={0};
	static int len=0;
	int ret;
	struct i2c_msg msgs[] = {
		{.addr = i2c.client->addr, .flags = 0, .len = len, .buf = buffer,}
        };

	// parsing ioctl command
	switch(cmd){
	case ILITEK_IOCTL_I2C_WRITE_DATA:
		ret = copy_from_user(buffer, (unsigned char*)arg, len);
		if(ret < 0){
                	printk(ILITEK_ERROR_LEVEL "%s, copy data from user space, failed\n", __func__);
                	return -1;
        	}
		ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
		if(ret < 0){
			printk(ILITEK_ERROR_LEVEL "%s, i2c write, failed\n", __func__);
			return -1;
		}
		break;
	case ILITEK_IOCTL_I2C_READ_DATA:
		msgs[0].flags = I2C_M_RD;
		ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
		if(ret < 0){
                        printk(ILITEK_ERROR_LEVEL "%s, i2c read, failed\n", __func__);
			return -1;
                }
		ret = copy_to_user((unsigned char*)arg, buffer, len);
		if(ret < 0){
                        printk(ILITEK_ERROR_LEVEL "%s, copy data to user space, failed\n", __func__);
                        return -1;
                }
		break;
	case ILITEK_IOCTL_I2C_WRITE_LENGTH:
	case ILITEK_IOCTL_I2C_READ_LENGTH:
		len = arg;
		break;
	case ILITEK_IOCTL_I2C_UPDATE_RESOLUTION:
	case ILITEK_IOCTL_I2C_SET_ADDRESS:
	case ILITEK_IOCTL_I2C_UPDATE:
		break;
	case ILITEK_IOCTL_START_READ_DATA:
		i2c.stop_polling = 0;
		break;
	case ILITEK_IOCTL_STOP_READ_DATA:
		i2c.stop_polling = 1;
                break;
	default:
		return -1;
	}
    	return 0;
}

static ssize_t
ilitek_file_read(
        struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	return 0;
}
static int 
ilitek_file_close(
    struct inode *inode, struct file *filp)
{
    return 0;
}


#define DRIVER_NAME "itk"
#define DRIVER_VERSION "1"

//#define ITK_TS_DEBUG_REPORT
//#define ITK_TS_DEBUG_READ
//#define ITK_TS_DEBUG_INFO
//#define TS_DELAY_WORK

/* periodic polling delay and period */
#define TS_POLL_DELAY   (1 * 1000000)
#define TS_POLL_PERIOD  (5 * 1000000)

#define MAX_SUPPORT_POINT   5 //just support 2 point now
#define ITK_INFO_ADDR       0x10
#define ITK_INFO_LEN        9

/**
 * struct ts_event - touchscreen event structure
 * @contactid:  num id
 * @pendown:    state of the pen
 * @valid:      is valid data
 * @x:          X-coordinate of the event
 * @y:          Y-coordinate of the event
 * @z:          pressure of the event
 */
struct ts_event {
        short contactid;
        short pendown;
        short valid;
        short x;
        short y;
        short xz;
        short yz;
        short xw;
        short yw;
};

/**
 * struct itk - touchscreen controller context
 * @client:         I2C client
 * @input:          touchscreen input device
 * @lock:           lock for resource protection
 * @timer:          timer for periodical polling
 * @work:           workqueue structure
 * @event[]:        touchscreen event buff
 * @pendown:        current pen state
 * @touching_num:   count for check touching fingers
 * @lcd_xmax:       lcd resolution
 * @lcd_ymax:       lcd resolution
 * @tp_xmax:        max virtual resolution
 * @tp_ymax:        max virtual resolution
 * @pdata:          platform-specific information
 * @running:		workqueue is running 
 * @work_exit:		the workqueue need to exit
 */
struct itk {
       struct i2c_client *client;
       struct input_dev *input;
       spinlock_t lock;
       struct hrtimer timer;
#ifdef TS_DELAY_WORK
       struct delayed_work work;
#else
       struct work_struct work;
       struct workqueue_struct *workqueue;
#endif
       struct ts_event event[MAX_SUPPORT_POINT];
       unsigned pendown:1;
       unsigned touching_num;
       int lcd_xmax;
       int lcd_ymax;
       int tp_xmax;
       int tp_ymax;
       struct itk_platform_data *pdata;
		int running;			//add by sz.zhuw 20110927
		int work_exit;
};

static int 
ilitek_i2c_transfer(
	struct i2c_client *client, struct i2c_msg *msgs, int cnt)
{
	int ret, count=ILITEK_I2C_RETRY_COUNT;
	while(count >= 0){
		count-= 1;
		ret = down_interruptible(&wr_sem);
                ret = i2c_transfer(client->adapter, msgs, cnt);
                up(&wr_sem);
                if(ret < 0){
                        msleep(500);
			continue;
                }
		break;
	}
	return ret;
}
/**
 * itk_get_pendown_state() - obtain the current pen state
 * @ts: touchscreen controller context
 */
static int itk_get_pendown_state(struct itk *ts)
{
    int state = 0;

    if (ts && ts->pdata && ts->pdata->get_irq_level)
        state = !ts->pdata->get_irq_level();

    return state;
}

static int itk_register_input(struct itk *ts)
{
    int ret;
    struct input_dev *dev;

    dev = input_allocate_device();
    if (dev == NULL)
        return -1;

    dev->name = "Touch Screen";
    dev->phys = "I2C";
    dev->id.bustype = BUS_I2C;
    dev->id.vendor = 0x222a;
    dev->id.product = 0x0001;
    dev->id.version = 0x0001;

    set_bit(EV_ABS, dev->evbit);
    set_bit(EV_KEY, dev->evbit);
    set_bit(BTN_TOUCH, dev->keybit);
    set_bit(ABS_MT_TOUCH_MAJOR, dev->absbit);
    set_bit(ABS_MT_WIDTH_MAJOR, dev->absbit);
    set_bit(ABS_MT_POSITION_X, dev->absbit);
    set_bit(ABS_MT_POSITION_Y, dev->absbit);
    set_bit(ABS_MT_TRACKING_ID, dev->absbit);
    //set_bit(ABS_MT_PRESSURE, dev->absbit);

    input_set_abs_params(dev, ABS_X, 0, ts->tp_xmax, 0, 0);
    input_set_abs_params(dev, ABS_Y, 0, ts->tp_ymax, 0, 0);
    input_set_abs_params(dev, ABS_MT_POSITION_X, 0, ts->tp_xmax, 0, 0);
    input_set_abs_params(dev, ABS_MT_POSITION_Y, 0, ts->tp_ymax, 0, 0);
    input_set_abs_params(dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(dev, ABS_MT_TRACKING_ID, 0, 10, 0, 0);
    //input_set_abs_params(dev, ABS_MT_PRESSURE, 0, ???, 0, 0);

    ret = input_register_device(dev);
    if (ret < 0) {
        input_free_device(dev);
        return -1;
    }
    
    ts->input = dev;
    return 0;
}

static int itk_read_block(struct i2c_client *client, u8 addr, u8 len, u8 *data)
{
    int ret;
    u8 msgbuf0[1] = { addr };
    u16 slave = client->addr;
    u16 flags = client->flags;
    struct i2c_msg msg[2] = { 
        { slave, flags, 1, msgbuf0 },
        { slave, flags|I2C_M_RD, len, data }
    };

    ret = down_interruptible(&wr_sem);
    ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
    up(&wr_sem);
    return ret;
}

/* //just mark for not used warning
static int itk_write_block(struct i2c_client *client, u8 addr, u8 len, u8 *data)
{
    u8 msgbuf0[1] = { addr };
    u16 slave = client->addr;
    u16 flags = client->flags;
    struct i2c_msg msg[2] = {
        { slave, flags, 1, msgbuf0 },
        { slave, flags, len, data }
    };

    return i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
}
*/

static void itk_reset(struct itk *ts)
{
    int i = 0;
    if (NULL == ts)
        return;
    memset(ts->event, 0, sizeof(struct ts_event)*MAX_SUPPORT_POINT);
    for (i=0; i<MAX_SUPPORT_POINT; i++)
    {
        ts->event[i].pendown = -1;
    }
    return;
}

static int itk_read_sensor(struct itk *ts)
{
    int ret=-1, status = 0;
    u8 data[ITK_INFO_LEN];

    /* To ensure data coherency, read the sensor with a single transaction. */
    ret = itk_read_block(ts->client, ITK_INFO_ADDR, ITK_INFO_LEN, data);
    if (ret < 0) {
        dev_err(&ts->client->dev, "Read block failed: %d\n", ret);
        return ret;
    }
    ret = 0;
    status = data[0]&0x3;
    ts->event[0].x = data[2]<<8|data[1];
    ts->event[0].y = data[4]<<8|data[3];
    ts->event[1].x = data[6]<<8|data[5];
    ts->event[1].y = data[8]<<8|data[7];
    
    if(status & 0x1)
    {
	    if((ts->event[0].x > (ts->pdata->tp_max_width - 4)) || (ts->event[0].x < 4))
	    		return 2;//status = status & 2; 
			
			if((ts->event[0].y > (ts->pdata->tp_max_height - 4)) || (ts->event[0].y < 4))
					return 2;//status = status & 2;
		}
		if(status & 0x2)
		{
			if((ts->event[1].x > (ts->pdata->tp_max_width - 4)) || (ts->event[1].x < 4))
					return 2;//status = status & 1;
			
			if((ts->event[1].y > (ts->pdata->tp_max_height - 4)) || (ts->event[1].y < 4))
					return 2;//status = status & 1;
		}

		
		if (ts->pdata->swap_xy){
			swap(ts->event[0].x, ts->event[0].y);
			swap(ts->event[1].x, ts->event[1].y);
		}
		if (ts->pdata->xpol){
    	ts->event[0].x = ts->pdata->tp_max_width - ts->event[0].x;
    	ts->event[1].x = ts->pdata->tp_max_width - ts->event[1].x;
    }
		if (ts->pdata->ypol){
    	ts->event[0].y = ts->pdata->tp_max_height - ts->event[0].y;
    	ts->event[1].y = ts->pdata->tp_max_height - ts->event[1].y;
		}
    ts->touching_num = status;
    #ifdef ITK_TS_DEBUG_READ
    printk(KERN_INFO "\nread_sensor status = %d, event[0]->x = %d, event[0]->y = %d, event[1]->x = %d, event[1]->y = %d\n", 
        ts->touching_num, ts->event[0].x, ts->event[0].y, ts->event[1].x, ts->event[1].y);
    #endif
    return 0;
}

/**
 * itk_work() - work queue handler (initiated by the interrupt handler)
 * @work:      work queue to handle
 */
static void itk_work(struct work_struct *work)
{
#ifdef TS_DELAY_WORK
    struct itk *ts = container_of(to_delayed_work(work), struct itk, work);
#else
    struct itk *ts = container_of(work, struct itk, work);
#endif
    struct ts_event *event;
    int i = 0, j = 1;
    int ret = -1;

	if(ts->work_exit){
		ts->running	= 0;
		ts->work_exit	= 0;
		return;
	}

    if (itk_get_pendown_state(ts)) 
    {
    		ret = itk_read_sensor(ts);
        if (ret < 0) {
            printk(KERN_INFO "work read i2c failed\n");
            goto restart;
        }
        else if(ret == 2)  //有手超過邊界...所有點都不上報
        {
        	ts->touching_num = 0;
        }
    
        if (!ts->pendown) {
            ts->pendown = 1;
            #ifdef ITK_TS_DEBUG_INFO
            printk(KERN_INFO "DOWN\n");
            #endif
            input_report_key(ts->input, BTN_TOUCH,  1);
        }
        switch (ts->touching_num)
        {
            case 0x1:
                i = 0;
                j = 1;
                break;
            case 0x2:
                i = 1;
                j = 2;
                break;
            case 0x3:
                i = 0;
                j = 2;
                break;
            case 0x0:
            default:
                i = 0;
                j = 0;
                break;
        }
        for (; i<j; i++)
        {
            event = &ts->event[i];

            input_report_abs(ts->input, ABS_MT_TRACKING_ID, i);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "\nreport ABS_MT_TRACKING_ID %d\n", i);
            #endif

            input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 1);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "report ABS_MT_TOUCH_MAJOR %d\n", 1);
            #endif

            input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 0);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "report ABS_MT_WIDTH_MAJOR %d\n", 0);
            #endif

            input_report_abs(ts->input, ABS_MT_POSITION_X, event->x);
            input_report_abs(ts->input, ABS_MT_POSITION_Y, event->y);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "report ABS_MT_POSITION_XY %d,%d\n", event->x, event->y);
            #endif

            input_mt_sync(ts->input);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "input_mt_sync\n");
            #endif

            if (ts->touching_num == 0x3)
            {
                if (i == 0)
                    continue;
            }
            input_sync(ts->input);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "input_sync\n");
            #endif
        }
restart:
#ifdef TS_DELAY_WORK
        schedule_delayed_work(&ts->work, msecs_to_jiffies(TS_POLL_PERIOD));
#else
        hrtimer_start(&ts->timer, ktime_set(0, TS_POLL_PERIOD), HRTIMER_MODE_REL);
#endif
    }
    else {
        /* enable IRQ after the pen was lifted */       
        if (ts->pendown) {
            ts->pendown = 0;
            #ifdef ITK_TS_DEBUG_INFO
            printk(KERN_INFO "UP\n");
            #endif
            input_report_key(ts->input, BTN_TOUCH,  0);
            input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 0);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "report ABS_MT_TOUCH_MAJOR %d\n", 0);
            #endif
            input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 0);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "report ABS_MT_WIDTH_MAJOR %d\n", 0);
            #endif
            input_mt_sync(ts->input);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "input_mt_sync\n");
            #endif
            input_sync(ts->input);
            #ifdef ITK_TS_DEBUG_REPORT
            printk(KERN_INFO "input_sync\n");
            #endif
            itk_reset(ts);
        }
        ts->touching_num = 0;
        enable_irq(ts->client->irq);
		ts->running	= 0;
    }
}

#ifndef TS_DELAY_WORK
/**
 * itk_timer() - timer callback function
 * @timer: timer that caused this function call
 */
static enum hrtimer_restart itk_timer(struct hrtimer *timer)
{
    struct itk *ts = container_of(timer, struct itk, timer);
    unsigned long flags = 0;
    ts->running	= 1;
    spin_lock_irqsave(&ts->lock, flags);
//  printk(KERN_INFO "enter timer\n");
    queue_work(ts->workqueue, &ts->work);
    spin_unlock_irqrestore(&ts->lock, flags);
    return HRTIMER_NORESTART;
}
#endif

/**
 * itk_interrupt() - interrupt handler for touch events
 * @irq: interrupt to handle
 * @dev_id: device-specific information
 */
static irqreturn_t itk_interrupt(int irq, void *dev_id)
{
    struct i2c_client *client = (struct i2c_client *)dev_id;
    struct itk *ts = i2c_get_clientdata(client);
    unsigned long flags;
    
    spin_lock_irqsave(&ts->lock, flags);
    #ifdef ITK_TS_DEBUG_REPORT
    printk(KERN_INFO "itk_interrupt() enter penirq\n");
    #endif
    /* if the pen is down, disable IRQ and start timer chain */
    if (itk_get_pendown_state(ts)) {
        disable_irq_nosync(client->irq);
		ts->running	= 1;
#ifdef TS_DELAY_WORK
        schedule_delayed_work(&ts->work, msecs_to_jiffies(TS_POLL_DELAY));
#else
        hrtimer_start(&ts->timer, ktime_set(0, TS_POLL_DELAY), HRTIMER_MODE_REL);
#endif
    }
    spin_unlock_irqrestore(&ts->lock, flags);
    return IRQ_HANDLED;
}

/**
 * itk_probe() - initialize the I2C client
 * @client: client to initialize
 * @id: I2C device ID
 */

struct itk_platform_data * itk_data;

#ifdef CONFIG_HAS_EARLYSUSPEND
static void aml_itk_early_suspend(struct early_suspend *h)
{
    int ret;
    uint8_t cmd = ILITEK_TP_CMD_SET_SLEEP_MODE;
    struct itk *ts = (struct itk *)(h->param);

    struct i2c_msg msgs_cmd[] = {
        { .addr = i2c.client->addr, .flags = 0, .len = 1, .buf = &cmd, },
    };

    printk(KERN_INFO "%s() enter\n", __FUNCTION__);

    if (ts->running) {
        ts->work_exit = 1;
        mdelay(10);
    }
    else {
        disable_irq(i2c.client->irq);
    }	

    printk(ILITEK_DEBUG_LEVEL "%s() Disable i2c IRQ\n", __func__);
  
    ret = ilitek_i2c_transfer(i2c.client, msgs_cmd, 1);
    if (ret < 0)
        printk(KERN_ERR "%s() i2c write error %d\n", __func__, ret);
}

static void aml_read_itk_version(void)
{
	u8 data_fv[3];
	u8 data_pv[2];
	int ret;
	ret	= itk_read_block(i2c.client, 0x40, 3, data_fv);
	if(ret < 0){
		printk("%s read Firmware Version failed\n",__FUNCTION__);
	}else{
		printk("%s read Firmware:%d:%d:%d\n",__FUNCTION__,data_fv[0],data_fv[1],data_fv[2]);
	}
	ret	= itk_read_block(i2c.client, 0x42, 2, data_pv);
	if(ret < 0){
		printk("%s read Protocol Version failed\n",__FUNCTION__);
	}else{
		printk("%s read Protocol:%d:%d\n",__FUNCTION__,data_pv[0],data_pv[1]);
	}
}

static void aml_itk_once_reset(void)
{
	int ret;
	uint8_t cmd = ILITEK_TP_CMD_SET_SLEEP_MODE;

	struct i2c_msg msgs_cmd[] = {
	{
		.addr = i2c.client->addr, .flags = 0, .len = 1, .buf = &cmd,},
	};

	ret = ilitek_i2c_transfer(i2c.client, msgs_cmd, 1);
	mdelay(5);

	if(itk_data && itk_data->get_irq_level){	
		itk_data->touch_on(0);
		mdelay(20);
		itk_data->touch_on(1);
		mdelay(10);
		printk("%s\n",__FUNCTION__);
	}	
}
static void aml_itk_late_resume(struct early_suspend *h)
{
	printk("enter -----> %s \n",__FUNCTION__);
	if(itk_data->touch_on){
			
      itk_data->touch_on(0);
      msleep(20);
      
      itk_data->touch_on(1);
      msleep(10);
      enable_irq(i2c.client->irq);
                printk(ILITEK_DEBUG_LEVEL "%s, disable i2c irq\n", __func__);
	  }
}
#endif

static int itk_probe(struct i2c_client *client,
    const struct i2c_device_id *id)
{
    struct itk *ts;
    int err = 0;

    ts = kzalloc(sizeof(struct itk), GFP_KERNEL);
    if (!ts) {
        err = -ENOMEM;
        goto fail;
    }

    ts->client = client;
    i2c.client = client;
    itk_reset(ts);

    /* setup platform-specific hooks */
    ts->pdata = (struct itk_platform_data*)client->dev.platform_data;
    itk_data = (struct itk_platform_data*)client->dev.platform_data;
    if (!ts->pdata || !ts->pdata->init_irq || !ts->pdata->get_irq_level) {
        dev_err(&client->dev, "no platform-specific callbacks "
            "provided\n");
        err = -ENXIO;
        goto fail;
    }
    else
    {
        ts->lcd_xmax = ((struct itk_platform_data*) client->dev.platform_data)->lcd_max_width;
        ts->lcd_ymax = ((struct itk_platform_data*) client->dev.platform_data)->lcd_max_height;
        ts->tp_xmax = ((struct itk_platform_data*) client->dev.platform_data)->tp_max_width;
        ts->tp_ymax = ((struct itk_platform_data*) client->dev.platform_data)->tp_max_height;
    }

    if (itk_register_input(ts) < 0) {
        dev_err(&client->dev, "register input fail!\n");
        goto fail;
    }

#ifdef CONFIG_HAS_EARLYSUSPEND
	aml_itk_once_reset();
	aml_read_itk_version();
#endif
	ts->running	= 0;
	ts->work_exit	= 0;
	
	
    if (ts->pdata->init_irq) {
        err = ts->pdata->init_irq();
        if (err < 0) {
            dev_err(&client->dev, "failed to initialize IRQ#%d: "
                "%d\n", client->irq, err);
            goto fail;
        }
    }
    memcpy(&i2c_dev, client, sizeof(struct i2c_client));

    spin_lock_init(&ts->lock);
#ifdef TS_DELAY_WORK
    INIT_DELAYED_WORK(&ts->work, itk_work);
#else
    hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    ts->timer.function = itk_timer;
    INIT_WORK(&ts->work, itk_work);
    ts->workqueue = create_singlethread_workqueue("itk");
    if (ts->workqueue == NULL) {
        dev_err(&client->dev, "can't create work queue\n");
        err = -ENOMEM;
        goto fail;
    }
    #ifdef ITK_TS_DEBUG_REPORT
    printk("work create: %x\n", ts->workqueue);
    #endif
#endif
      
    ts->pendown = 0;
    ts->touching_num = 0;

    err = request_irq(client->irq, itk_interrupt, IRQF_TRIGGER_FALLING,
        client->dev.driver->name, client);
    if (err) {
        dev_err(&client->dev, "failed to request IRQ#%d: %d\n",
        client->irq, err);
        goto fail_irq;
    }

#ifdef CONFIG_HAS_EARLYSUSPEND
    itk_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
    itk_early_suspend.suspend = aml_itk_early_suspend;
    itk_early_suspend.resume = aml_itk_late_resume;
    itk_early_suspend.param = ts;
    register_early_suspend(&itk_early_suspend);
#endif

    i2c_set_clientdata(client, ts);
    //schedule_delayed_work(&ts->cal_work, 20*HZ);
    err = 0;
    goto out;	

fail_irq:
    free_irq(client->irq, client);

fail:
    if (ts) {
        input_free_device(ts->input);
        kfree(ts);
    }

    i2c_set_clientdata(client, NULL);
out:
    itk_read_sensor(ts);
    itk_reset(ts);
    itk_data->touch_on(0);
    msleep(10);
      
    itk_data->touch_on(1);
    printk("itk touch screen driver ok\n");
    return err;
}

/**
 * itk_remove() - cleanup the I2C client
 * @client: client to clean up
 */
static int itk_remove(struct i2c_client *client)
{
    struct itk *priv = i2c_get_clientdata(client);

    free_irq(client->irq, client);
    i2c_set_clientdata(client, NULL);
    input_unregister_device(priv->input);
    kfree(priv);
    #ifdef CONFIG_HAS_EARLYSUSPEND
      unregister_early_suspend(&itk_early_suspend);
    #endif
    return 0;
}

static const struct i2c_device_id itk_ids[] = {
    { DRIVER_NAME, 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, itk_ids);
/* ITK I2C Capacitive Touch Screen driver */
static struct i2c_driver itk_driver = {
    .driver = {
    .name = DRIVER_NAME,
    .owner = THIS_MODULE,
    },
    .probe = itk_probe,
    .remove = __devexit_p(itk_remove),
    .id_table = itk_ids,
};

/**
 * itk_init() - module initialization
 */
static int __init itk_init(void)
{
    int ret = 0;
    sema_init(&wr_sem, 1);

    ret = i2c_add_driver(&itk_driver);
    ret |= alloc_chrdev_region(&dev.devno, 0, 1, ILITEK_FILE_DRIVER_NAME);
    if(ret){
        printk("%s, can't alloc chrdev\n", __func__);
    }
    printk("%s, register chrdev(%d, %d)\n", __func__, MAJOR(dev.devno), MINOR(dev.devno));
    
    cdev_init(&dev.cdev, &ilitek_fops);
    dev.cdev.owner = THIS_MODULE;
    ret |= cdev_add(&dev.cdev, dev.devno, 1);
    if(ret < 0){
        printk("%s, add char devive error, ret %d\n", __func__, ret);
    }
    dev.class = class_create(THIS_MODULE, ILITEK_FILE_DRIVER_NAME);
    if(IS_ERR(dev.class)){
        printk("%s, creating class error\n", __func__);
    }
    device_create(dev.class, NULL, dev.devno, NULL, "ilitek_ctrl");

    return ret;
}

/**
 * itk_exit() - module cleanup
 */
static void __exit itk_exit(void)
{
    i2c_del_driver(&itk_driver);
    cdev_del(&dev.cdev);
    unregister_chrdev_region(dev.devno, 1);
    device_destroy(dev.class, dev.devno);
    class_destroy(dev.class);
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(itk_init);
#else
module_init(itk_init);
#endif
module_exit(itk_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("itk I2C Capacitive Touch Screen driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);


