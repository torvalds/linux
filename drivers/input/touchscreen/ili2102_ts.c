#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/types.h>
#include <mach/iomux.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>
#include "ili2102_ts.h"

static int  ts_dbg_enable = 0;

#define DBG(msg...) \
	({if(ts_dbg_enable == 1) printk(msg);})
		
#define TOUCH_NUMBER 2

static volatile int touch_state[TOUCH_NUMBER] = {TOUCH_UP,TOUCH_UP};
static volatile unsigned int g_x[TOUCH_NUMBER] =  {0},g_y[TOUCH_NUMBER] = {0};

struct ili2102_ts_data {
	u16		model;			/* 801. */	
	bool	swap_xy;		/* swap x and y axes */	
	u16		x_min, x_max;	
	u16		y_min, y_max;
	uint16_t addr;
	int 	use_irq;
	int	pendown;
	int 	gpio_pendown;
	int 	gpio_reset;
	int 	gpio_reset_active_low;
	int		pendown_iomux_mode;	
	int		resetpin_iomux_mode;
	char	pendown_iomux_name[IOMUX_NAME_SIZE];	
	char	resetpin_iomux_name[IOMUX_NAME_SIZE];	
	char	phys[32];
	char	name[32];
	int		valid_i2c_register;
	struct 	i2c_client *client;
	struct 	input_dev *input_dev;
	struct 	hrtimer timer;
	struct 	delayed_work	work;
	struct 	workqueue_struct *ts_wq;	
	struct 	early_suspend early_suspend;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ili2102_ts_early_suspend(struct early_suspend *h);
static void ili2102_ts_late_resume(struct early_suspend *h);
#endif

#define ILI2102_TS_APK_SUPPORT 1

#if ILI2102_TS_APK_SUPPORT
// device data
struct dev_data {
        // device number
        dev_t devno;
        // character device
        struct cdev cdev;
        // class device
        struct class *class;
};

// global variables
static struct ili2102_ts_data *g_ts;
static struct dev_data g_dev;

// definitions
#define ILITEK_I2C_RETRY_COUNT			3
#define ILITEK_FILE_DRIVER_NAME			"ilitek_file"
#define ILITEK_DEBUG_LEVEL			KERN_INFO
#define ILITEK_ERROR_LEVEL			KERN_ALERT

// i2c command for ilitek touch screen
#define ILITEK_TP_CMD_READ_DATA			0x10
#define ILITEK_TP_CMD_READ_SUB_DATA		0x11
#define ILITEK_TP_CMD_GET_RESOLUTION		0x20
#define ILITEK_TP_CMD_GET_FIRMWARE_VERSION	0x40
#define ILITEK_TP_CMD_GET_PROTOCOL_VERSION	0x42
#define	ILITEK_TP_CMD_CALIBRATION		0xCC
#define ILITEK_TP_CMD_ERASE_BACKGROUND		0xCE

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


static ssize_t ili2102_proc_write(struct file *file, const char __user *buffer,
			   unsigned long count, void *data)
{
	char c;
	int rc;
	
	rc = get_user(c, buffer);
	if (rc)
		return rc; 
	
	if (c == '1')
		ts_dbg_enable = 1; 
	else if (c == '0')
		ts_dbg_enable = 0; 

	return count; 
}

static const struct file_operations ili2102_proc_fops = {
	.owner		= THIS_MODULE, 
	.write		= ili2102_proc_write,
}; 

static int ilitek_file_open(struct inode *inode, struct file *filp)
{
	return 0; 
}

static ssize_t ilitek_file_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	int ret;
	unsigned char buffer[128]={0};
	struct i2c_msg msg[2];

	msg[0].addr = g_ts->client->addr;
	msg[0].flags = g_ts->client->flags;
	msg[0].len = count;
	msg[0].buf = buffer;
	msg[0].scl_rate = 400*1000;
	msg[0].udelay = 80;

	DBG("%s:count=0x%x\n",__FUNCTION__,count);
	
	// before sending data to touch device, we need to check whether the device is working or not
	if(g_ts->valid_i2c_register == 0){
		printk(ILITEK_ERROR_LEVEL "%s, i2c device driver doesn't be registered\n", __func__);
		return -1;
	}

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
        msg[0].len = 1;
        ret = i2c_transfer(g_ts->client->adapter, msg, 1);
        if(ret < 0){
                printk(ILITEK_DEBUG_LEVEL "%s, i2c erase background, failed\n", __func__);
        }
        else{
                printk(ILITEK_DEBUG_LEVEL "%s, i2c erase background, success\n", __func__);
        }

		buffer[0] = ILITEK_TP_CMD_CALIBRATION;
        msg[0].len = 1;
        msleep(2000);
        ret = i2c_transfer(g_ts->client->adapter, msg, 1);
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


//static int ilitek_file_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
long ilitek_file_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)

{
	static unsigned char buffer[64]={0};
	static int len=0;
	int ret;
	struct i2c_msg msg[2];
	
	msg[0].addr = g_ts->client->addr;
	msg[0].flags = g_ts->client->flags;
	msg[0].len = len;
	msg[0].buf = buffer;
	msg[0].scl_rate = 400*1000;
	msg[0].udelay = 80;
	
	// parsing ioctl command
	switch(cmd){
	case ILITEK_IOCTL_I2C_WRITE_DATA:
		ret = copy_from_user(buffer, (unsigned char*)arg, len);
		if(ret < 0){
    	printk(ILITEK_ERROR_LEVEL "%s, copy data from user space, failed\n", __func__);
    	return -1;
        }
		ret = i2c_transfer(g_ts->client->adapter, msg, 1);
		if(ret < 0){
		printk(ILITEK_ERROR_LEVEL "%s, i2c write, failed\n", __func__);
		return -1;
		}
		break;
		
	case ILITEK_IOCTL_I2C_READ_DATA:
		msg[0].addr = g_ts->client->addr;
		msg[0].flags = g_ts->client->flags | I2C_M_RD;
		msg[0].len = len;	
		msg[0].buf = buffer;
		msg[0].scl_rate = 400*1000;
		msg[0].udelay = 80;
		ret = i2c_transfer(g_ts->client->adapter, msg, 1);
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
		//g_ts.stop_polling = 0;
		break;
	case ILITEK_IOCTL_STOP_READ_DATA:
		//g_ts.stop_polling = 1;
                break;
	default:
		return -1;
	}
	
	DBG("%s:cmd=0x%x\n",__FUNCTION__,cmd);
	
    	return 0;
}


static ssize_t ilitek_file_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	return 0;
}


static int ilitek_file_close(struct inode *inode, struct file *filp)
{
        return 0;
}


// declare file operations
struct file_operations ilitek_fops = {
	.unlocked_ioctl = ilitek_file_ioctl,	
	.read = ilitek_file_read,
	.write = ilitek_file_write,
	.open = ilitek_file_open,
	.release = ilitek_file_close,
};

#endif
static int verify_coord(struct ili2102_ts_data *ts,unsigned int *x,unsigned int *y)
{

	//DBG("%s:(%d/%d)\n",__FUNCTION__,*x, *y);
	#ifndef CONFIG_MACH_RK29_TD8801_V2
	if((*x< ts->x_min) || (*x > ts->x_max))
		return -1;

	if((*y< ts->y_min) || (*y > ts->y_max))
		return -1;
    #endif

	/*android do not support min and max value*/
	if(*x == ts->x_min)
		*x = ts->x_min + 1;
	if(*y == ts->y_min)
		*y = ts->y_min + 1;
	if(*x == ts->x_max)
		*x = ts->x_max - 1;
	if(*y == ts->y_max)
		*y = ts->y_max - 1;
	

	return 0;
}
static int ili2102_init_panel(struct ili2102_ts_data *ts)
{	
	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_LOW:GPIO_HIGH);
	mdelay(1);
	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);
	return 0;
}

static void ili2102_ts_work_func(struct work_struct *work)
{
	int i,ret,num=1;
	int syn_flag = 0;
	unsigned int x, y;
	struct i2c_msg msg[2];
	uint8_t start_reg;
	uint8_t buf[9];//uint32_t buf[4];
	struct ili2102_ts_data *ts = container_of(work, struct ili2102_ts_data, work);

	DBG("ili2102_ts_work_func\n");

	/*Touch Information Report*/
	start_reg = 0x10;

	msg[0].addr = ts->client->addr;
	msg[0].flags = ts->client->flags;
	msg[0].len = 1;
	msg[0].buf = &start_reg;
	msg[0].scl_rate = 200*1000;
	msg[0].udelay = 200;
	
	msg[1].addr = ts->client->addr;
	msg[1].flags = ts->client->flags | I2C_M_RD;
	msg[1].len = 9;	
	msg[1].buf = buf;
	msg[1].scl_rate = 200*1000;
	msg[1].udelay = 0;
	
	ret = i2c_transfer(ts->client->adapter, msg, 2); 
	if (ret < 0) 
	{
		printk("%s:i2c_transfer fail, ret=%d\n",__FUNCTION__,ret);
		goto out;
	}
	
	for(i=0; i<TOUCH_NUMBER; i++)
	{

		if(!((buf[0]>>i)&0x01))
		{
		  	if (touch_state[i] == TOUCH_DOWN)
		  	{
				DBG("ili2102_ts_work_func:buf[%d]=%d\n",i,buf[i]);
				//input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
				//input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
				input_mt_slot(ts->input_dev, i);
				input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
				syn_flag = 1;
				touch_state[i] = TOUCH_UP;
				DBG("i=%d,touch_up \n",i);
			}

		}
		else
		{
			if((buf[0]>>i)&0x01)
			{
				x = buf[1+(i<<2)] | (buf[2+(i<<2)] << 8);
				y = buf[3+(i<<2)] | (buf[4+(i<<2)] << 8);
				
				if (ts->swap_xy)
				swap(x, y);

				if (verify_coord(ts,&x,&y))//goto out;
				{
					printk("err:x=%d,y=%d\n",x,y);
					x = g_x[i];
					y = g_y[i];
				}
				#ifdef CONFIG_MACH_RK29_TD8801_V2
				if( y >=80 ) y-=80;
				if( x >= 50 ) x-=50;
				#endif

				g_x[i] = x;
				g_y[i] = y;	
				
				input_mt_slot(ts->input_dev, i);
				input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
				//input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 100);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	
				syn_flag = 1;
				touch_state[i] = TOUCH_DOWN;
				 ts->pendown = 1;
				DBG("touch_down i=%d X = %d, Y = %d\n",i, x, y);
			}
			
		}
	}
	
	if(syn_flag)
	input_sync(ts->input_dev);
out:   
#if 0
	if(ts->pendown)
	{
		schedule_delayed_work(&ts->work, msecs_to_jiffies(12));
		ts->pendown = 0;
	}
	else
	{
		if (ts->use_irq) 
		enable_irq(ts->client->irq);
	}
#else
	enable_irq(ts->client->irq);//intterupt pin will be high after i2c read so could enable irq at once
#endif
	DBG("pin=%d,level=%d,irq=%d\n\n",irq_to_gpio(ts->client->irq),gpio_get_value(irq_to_gpio(ts->client->irq)),ts->client->irq);

}

static irqreturn_t ili2102_ts_irq_handler(int irq, void *dev_id)
{
	struct ili2102_ts_data *ts = dev_id;
	DBG("ili2102_ts_irq_handler=%d,%d\n",ts->client->irq,ts->use_irq);

	disable_irq_nosync(ts->client->irq); //disable_irq(ts->client->irq);
	queue_delayed_work(ts->ts_wq, &ts->work, 0);
	return IRQ_HANDLED;
}

static int __devinit setup_resetPin(struct i2c_client *client, struct ili2102_ts_data *ts)
{
	struct ili2102_platform_data	*pdata = client->dev.platform_data;
	int err;
	
	ts->gpio_reset = pdata->gpio_reset;
	strcpy(ts->resetpin_iomux_name,pdata->resetpin_iomux_name);
	ts->resetpin_iomux_mode = pdata->resetpin_iomux_mode;
	ts->gpio_reset_active_low = pdata->gpio_reset_active_low;
	
	DBG("%s=%d,%s,%d,%d\n",__FUNCTION__,ts->gpio_reset,ts->resetpin_iomux_name,ts->resetpin_iomux_mode,ts->gpio_reset_active_low);

	if (!gpio_is_valid(ts->gpio_reset)) {
		dev_err(&client->dev, "no gpio_reset?\n");
		return -EINVAL;
	}

	rk29_mux_api_set(ts->resetpin_iomux_name,ts->resetpin_iomux_mode); 
	err = gpio_request(ts->gpio_reset, "ili2102_resetPin");
	if (err) {
		dev_err(&client->dev, "failed to request resetPin GPIO%d\n",
				ts->gpio_reset);
		return err;
	}
	
	//gpio_direction_output(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);

	err = gpio_direction_output(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_LOW:GPIO_HIGH);
	if (err) {
		dev_err(&client->dev, "failed to set resetPin GPIO%d\n",
				ts->gpio_reset);
		gpio_free(ts->gpio_reset);
		return err;
	}
	
	mdelay(5);

	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);

	mdelay(200);
	 
	return 0;
}

static int __devinit setup_pendown(struct i2c_client *client, struct ili2102_ts_data *ts)
{
	int err;
	struct ili2102_platform_data	*pdata = client->dev.platform_data;
	
	if (!client->irq) {
		dev_dbg(&client->dev, "no IRQ?\n");
		return -ENODEV;
	}
	
	if (!gpio_is_valid(pdata->gpio_pendown)) {
		dev_err(&client->dev, "no gpio_pendown?\n");
		return -EINVAL;
	}
	
	ts->gpio_pendown = pdata->gpio_pendown;
	strcpy(ts->pendown_iomux_name,pdata->pendown_iomux_name);
	ts->pendown_iomux_mode = pdata->pendown_iomux_mode;
	
	DBG("%s=%d,%s,%d\n",__FUNCTION__,ts->gpio_pendown,ts->pendown_iomux_name,ts->pendown_iomux_mode);
	
	if (!gpio_is_valid(ts->gpio_pendown)) {
		dev_err(&client->dev, "no gpio_pendown?\n");
		return -EINVAL;
	}
	
	rk29_mux_api_set(ts->pendown_iomux_name,ts->pendown_iomux_mode);
	err = gpio_request(ts->gpio_pendown, "ili2102_pendown");
	if (err) {
		dev_err(&client->dev, "failed to request pendown GPIO%d\n",
				ts->gpio_pendown);
		return err;
	}
	
	err = gpio_pull_updown(ts->gpio_pendown, PullDisable);
	if (err) {
		dev_err(&client->dev, "failed to pullup pendown GPIO%d\n",
				ts->gpio_pendown);
		gpio_free(ts->gpio_pendown);
		return err;
	}
	return 0;
}

static int ili2102_chip_Init(struct i2c_client *client)
{	
	int ret = 0;
	uint8_t start_reg;
	uint8_t buf[6];
	struct i2c_msg msg[2];
	
	/* get panel information:6bytes */
	start_reg = 0x20;
	msg[0].addr =client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &start_reg;
	msg[0].scl_rate = 400*1000;
	msg[0].udelay = 200;

	ret = i2c_transfer(client->adapter, msg, 1);   
	if (ret < 0) {
	printk("%s:err\n",__FUNCTION__);
	}
	
	mdelay(5);//tp need delay
	
	msg[0].addr = client->addr;
	msg[0].flags = client->flags |I2C_M_RD;
	msg[0].len = 6;
	msg[0].buf = (u8*)&buf[0];
	msg[0].scl_rate = 400*1000;
	msg[0].udelay = 200;

	ret = i2c_transfer(client->adapter, msg, 1);   
	if (ret < 0) {
	printk("%s:err\n",__FUNCTION__);
	}

	printk("%s:max_x=%d,max_y=%d,b[4]=0x%x,b[5]=0x%x\n", 
		__FUNCTION__,buf[0]|(buf[1]<<8),buf[2]|(buf[3]<<8),buf[4],buf[5]);

	/*get firmware version:3bytes */	
	start_reg = 0x40;
	msg[0].addr =client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &start_reg;
	msg[0].scl_rate = 400*1000;
	msg[0].udelay = 200;

	ret = i2c_transfer(client->adapter, msg, 1);   
	if (ret < 0) {
	printk("%s:err\n",__FUNCTION__);
	}
	
	mdelay(5);//tp need delay
	
	msg[0].addr = client->addr;
	msg[0].flags = client->flags | I2C_M_RD;
	msg[0].len = 3;
	msg[0].buf = (u8*)&buf[0];
	msg[0].scl_rate =400*1000;
	msg[0].udelay = 200;

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0) {
	printk("%s:err\n",__FUNCTION__);
	}

	printk("%s:Ver %d.%d.%d\n",__FUNCTION__,buf[0],buf[1],buf[2]);

	return ret;
    
}

static int ili2102_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ili2102_ts_data *ts;
	struct ili2102_platform_data	*pdata = client->dev.platform_data;
	int ret = 0;

	printk("ili2102 TS probe\n"); 
    
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
	    printk(KERN_ERR "ili2102_ts_probe: need I2C_FUNC_I2C\n");
	    ret = -ENODEV;
	    goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
	    ret = -ENOMEM;
	    goto err_alloc_data_failed;
	}
	
	ts->ts_wq = create_singlethread_workqueue("ts_wq");
    	if (!ts->ts_wq)
    	{
    		printk("%s:fail to create ts_wq,ret=0x%x\n",__FUNCTION__, ENOMEM);
        	return -ENOMEM;
    	}
	//INIT_WORK(&ts->work, ili2102_ts_work_func);
	INIT_DELAYED_WORK(&ts->work, ili2102_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);

	ret = setup_resetPin(client,ts);
	if(ret)
	{
		 printk("ili2102 TS setup_resetPin fail\n");
		 goto err_alloc_data_failed;
	}

	ret=ili2102_chip_Init(ts->client);
	if(ret<0)
	{
		printk("%s:chips init failed\n",__FUNCTION__);
		goto err_resetpin_failed;
	}

	/* allocate input device */
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
	    ret = -ENOMEM;
	    printk(KERN_ERR "ili2102_ts_probe: Failed to allocate input device\n");
	    goto err_input_dev_alloc_failed;
	}

	ts->model = pdata->model ? : 801;
	ts->swap_xy = pdata->swap_xy;
	ts->x_min = pdata->x_min;
	ts->x_max = pdata->x_max;
	ts->y_min = pdata->y_min;
	ts->y_max = pdata->y_max;
	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", dev_name(&client->dev));
	snprintf(ts->name, sizeof(ts->name), "ili%d-touchscreen", ts->model);
	ts->input_dev->phys = ts->phys;
	ts->input_dev->name = ts->name;
	ts->input_dev->dev.parent = &client->dev;
	ts->pendown = 0;
	ts->valid_i2c_register = 1;

	//ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_ABS);
	//ts->input_dev->absbit[0] = 
		//BIT(ABS_MT_POSITION_X) | BIT(ABS_MT_POSITION_Y) | 
		//BIT(ABS_MT_TOUCH_MAJOR) | BIT(ABS_MT_WIDTH_MAJOR);  // for android

	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	__set_bit(EV_ABS, ts->input_dev->evbit);
	
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 
		    ts->x_min ? : 0,
			ts->x_max ? : 480,
			0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
			ts->y_min ? : 0,
			ts->y_max ? : 800,
			0, 0);
	input_mt_init_slots(ts->input_dev, TOUCH_NUMBER);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	//input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);

	/* ts->input_dev->name = ts->keypad_info->name; */
	ret = input_register_device(ts->input_dev);
	if (ret) {
	    printk(KERN_ERR "ili2102_ts_probe: Unable to register %s input device\n", ts->input_dev->name);
	    goto err_input_register_device_failed;
	}

	client->irq = gpio_to_irq(client->irq);
	if (client->irq) 
	{
		ret = setup_pendown(client,ts);
		if(ret)
		{
			 printk("ili2102 TS setup_pendown fail\n");
			 goto err_input_register_device_failed;
		}
		
        ret = request_irq(client->irq, ili2102_ts_irq_handler, IRQF_DISABLED | IRQF_TRIGGER_LOW, client->name, ts);
        if (ret == 0) {
            DBG("ili2102 TS register ISR (irq=%d)\n", client->irq);
            ts->use_irq = 1;
        }
        else 
		dev_err(&client->dev, "request_irq failed\n");
    }
	
#if ILI2102_TS_APK_SUPPORT
	// initialize global variable
	g_ts = ts;
	memset(&g_dev, 0, sizeof(struct dev_data));	
	
	// allocate character device driver buffer
	ret = alloc_chrdev_region(&g_dev.devno, 0, 1, ILITEK_FILE_DRIVER_NAME);
    	if(ret){
        	printk(ILITEK_ERROR_LEVEL "%s, can't allocate chrdev\n", __func__);
		return ret;
	}
    	printk(ILITEK_DEBUG_LEVEL "%s, register chrdev(%d, %d)\n", __func__, MAJOR(g_dev.devno), MINOR(g_dev.devno));
	
	// initialize character device driver
	cdev_init(&g_dev.cdev, &ilitek_fops);
	g_dev.cdev.owner = THIS_MODULE;
    	ret = cdev_add(&g_dev.cdev, g_dev.devno, 1);
    	if(ret < 0){
        	printk(ILITEK_ERROR_LEVEL "%s, add character device error, ret %d\n", __func__, ret);
		return ret;
	}
	g_dev.class = class_create(THIS_MODULE, ILITEK_FILE_DRIVER_NAME);
	if(IS_ERR(g_dev.class)){
        	printk(ILITEK_ERROR_LEVEL "%s, create class, error\n", __func__);
		return ret;
    	}
    	device_create(g_dev.class, NULL, g_dev.devno, NULL, "ilitek_ctrl");
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = ili2102_ts_early_suspend;
	ts->early_suspend.resume = ili2102_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
		
	struct proc_dir_entry *ili2102_proc_entry;	
	ili2102_proc_entry = proc_create("driver/ili2102", 0777, NULL, &ili2102_proc_fops); 

	printk(KERN_INFO "ili2102_ts_probe: Start touchscreen %s in %s mode\n", ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");

	return 0;

	err_input_register_device_failed:
	input_free_device(ts->input_dev);
	err_resetpin_failed:
	gpio_free(ts->gpio_reset);
	err_input_dev_alloc_failed:
	kfree(ts);
	err_alloc_data_failed:
	err_check_functionality_failed:
	return ret;
}

static int ili2102_ts_remove(struct i2c_client *client)
{
	struct ili2102_ts_data *ts = i2c_get_clientdata(client);
	unregister_early_suspend(&ts->early_suspend);
	if (ts->use_irq)
	free_irq(client->irq, ts);
	else
	hrtimer_cancel(&ts->timer);
	input_unregister_device(ts->input_dev);
	if (ts->ts_wq)
	cancel_delayed_work_sync(&ts->work);
	kfree(ts);
	
#if ILI2102_TS_APK_SUPPORT
	// delete character device driver
	cdev_del(&g_dev.cdev);
	unregister_chrdev_region(g_dev.devno, 1);
	device_destroy(g_dev.class, g_dev.devno);
	class_destroy(g_dev.class);
#endif

	return 0;
}

static int ili2102_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct ili2102_ts_data *ts = i2c_get_clientdata(client);
	uint8_t buf[1] = {0x30};
	struct i2c_msg msg[1];

	//to do suspend
	msg[0].addr =client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf;

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0) {
	printk("%s:err\n",__FUNCTION__);
	}

	ret = cancel_delayed_work_sync(&ts->work);
	if (ret && ts->use_irq) /* if work was pending disable-count is now 2 */
	enable_irq(client->irq);
	
	if (ts->use_irq)
	{
		free_irq(client->irq, ts);
		//change irq type to IRQF_TRIGGER_FALLING to avoid system death
		ret = request_irq(client->irq, ili2102_ts_irq_handler, IRQF_DISABLED | IRQF_TRIGGER_FALLING, client->name, ts);
	    if (ret == 0) {
	       	disable_irq_nosync(client->irq);
	        ts->use_irq = 1;
	    }
	    else 
		printk("%s:request irq=%d failed,ret=%d\n",__FUNCTION__, ts->client->irq, ret);
	}
	else
	hrtimer_cancel(&ts->timer);

	DBG("%s\n",__FUNCTION__);
	
	return 0;
}


static void ili2102_ts_resume_work_func(struct work_struct *work)
{
	struct ili2102_ts_data *ts = container_of(work, struct ili2102_ts_data, work);
	int ret,i;

	PREPARE_DELAYED_WORK(&ts->work, ili2102_ts_work_func);
	mdelay(100); //wait for 100ms before i2c operation
	
	free_irq(ts->client->irq, ts);
	ret = request_irq(ts->client->irq, ili2102_ts_irq_handler, IRQF_DISABLED | IRQF_TRIGGER_LOW, ts->client->name, ts);
	if (ret == 0) {
	ts->use_irq = 1;
	//enable_irq(ts->client->irq);
	}
	else 
	printk("%s:request irq=%d failed,ret=%d\n",__FUNCTION__,ts->client->irq,ret);

	DBG("%s,irq=%d\n",__FUNCTION__,ts->client->irq);
}


static int ili2102_ts_resume(struct i2c_client *client)
{
    struct ili2102_ts_data *ts = i2c_get_clientdata(client);

    ili2102_init_panel(ts);
	
    if (ts->use_irq) {
        if(!delayed_work_pending(&ts->work)){
        	PREPARE_DELAYED_WORK(&ts->work, ili2102_ts_resume_work_func);
        	queue_delayed_work(ts->ts_wq, &ts->work, 0);
        }
    }
    else {
        hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
    }

	DBG("%s\n",__FUNCTION__);

    return 0;
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static void ili2102_ts_early_suspend(struct early_suspend *h)
{
    struct ili2102_ts_data *ts;
    ts = container_of(h, struct ili2102_ts_data, early_suspend);
    ili2102_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void ili2102_ts_late_resume(struct early_suspend *h)
{
    struct ili2102_ts_data *ts;
    ts = container_of(h, struct ili2102_ts_data, early_suspend);
    ili2102_ts_resume(ts->client);
}
#endif

#define ILI2102_TS_NAME "ili2102_ts"

static const struct i2c_device_id ili2102_ts_id[] = {
    { ILI2102_TS_NAME, 0 },
    { }
};

static struct i2c_driver ili2102_ts_driver = {
    .probe      = ili2102_ts_probe,
    .remove     = ili2102_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
    .suspend    = ili2102_ts_early_suspend,
    .resume     = ili2102_ts_late_resume,
#endif
    .id_table   = ili2102_ts_id,
    .driver = {
        .name   = ILI2102_TS_NAME,
    },
};

static int __devinit ili2102_ts_init(void)
{
    return i2c_add_driver(&ili2102_ts_driver);
}

static void __exit ili2102_ts_exit(void)
{
	i2c_del_driver(&ili2102_ts_driver);
}

module_init(ili2102_ts_init);
module_exit(ili2102_ts_exit);

MODULE_DESCRIPTION("ili2102 Touchscreen Driver");
MODULE_LICENSE("GPL");
