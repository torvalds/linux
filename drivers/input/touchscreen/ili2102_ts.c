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

#include "ili2102_ts.h"


#if 0
	#define DBG(msg...)	printk(msg);
#else
	#define DBG(msg...)
#endif

#define TOUCH_NUMBER 2

static int touch_state[TOUCH_NUMBER] = {TOUCH_UP,TOUCH_UP};
static unsigned int g_x[TOUCH_NUMBER] =  {0},g_y[TOUCH_NUMBER] = {0};

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

static int verify_coord(struct ili2102_ts_data *ts,unsigned int *x,unsigned int *y)
{

	DBG("%s:(%d/%d)\n",__FUNCTION__,*x, *y);
	if((*x< ts->x_min) || (*x > ts->x_max))
		return -1;

	if((*y< ts->y_min) || (*y > ts->y_max))
		return -1;

	return 0;
}
static int ili2102_init_panel(struct ili2102_ts_data *ts)
{	
	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);
	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_LOW:GPIO_HIGH);
	mdelay(1);
	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);
	mdelay(100);//need?
	return 0;
}

static void ili2102_ts_work_func(struct work_struct *work)
{
	int i,ret;
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
	msg[0].scl_rate = 400*1000;
	msg[0].udelay = 80;

	msg[1].addr = ts->client->addr;
	msg[1].flags = ts->client->flags | I2C_M_RD;
	msg[1].len = 9;	
	msg[1].buf = buf;//msg[1].buf = (u8*)&buf[0];
	msg[1].scl_rate = 400*1000;
	msg[1].udelay = 80;
	
	ret = i2c_transfer(ts->client->adapter, msg, 2); 
	if (ret < 0) 
	{
		//for(i=0; i<msg[1].len; i++) 
		//buf[i] = 0xff;
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
				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0); //Finger Size
				input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0); //Touch Size
				input_mt_sync(ts->input_dev);
				syn_flag = 1;
				touch_state[i] = TOUCH_UP;
				DBG("touch_up \n");
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

				g_x[i] = x;
				g_y[i] = y;			
				input_event(ts->input_dev, EV_ABS, ABS_MT_TRACKING_ID, i);
			        input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1); //Finger Size
			        input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
			        input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
			        input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 5); //Touch Size
			        input_mt_sync(ts->input_dev);
				syn_flag = 1;
				touch_state[i] = TOUCH_DOWN;
				 ts->pendown = 1;
				DBG("touch_down X = %d, Y = %d\n", x, y);
			}
			
		}
	}
	
	if(syn_flag)
	input_sync(ts->input_dev);
out:   
	if(ts->pendown)
	{
		schedule_delayed_work(&ts->work, msecs_to_jiffies(10));
		ts->pendown = 0;
	}
	else
	{
		if (ts->use_irq) 
		enable_irq(ts->client->irq);
	}

	DBG("pin=%d,level=%d,irq=%d\n",irq_to_gpio(ts->client->irq),gpio_get_value(irq_to_gpio(ts->client->irq)),ts->client->irq);

}

static irqreturn_t ili2102_ts_irq_handler(int irq, void *dev_id)
{
	struct ili2102_ts_data *ts = dev_id;
	DBG("ili2102_ts_irq_handler=%d,%d\n",ts->client->irq,ts->use_irq);

	disable_irq_nosync(ts->client->irq); //disable_irq(ts->client->irq);
	schedule_delayed_work(&ts->work, 0);
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
	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);
	mdelay(100);

	err = gpio_direction_output(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_LOW:GPIO_HIGH);
	if (err) {
		dev_err(&client->dev, "failed to pulldown resetPin GPIO%d\n",
				ts->gpio_reset);
		gpio_free(ts->gpio_reset);
		return err;
	}
	
	mdelay(1);

	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);

	mdelay(100);
	 
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
	
	err = gpio_pull_updown(ts->gpio_pendown, GPIOPullUp);
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
	msg[0].scl_rate = 200*1000;
	msg[0].udelay = 500;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags |I2C_M_RD;
	msg[1].len = 6;
	msg[1].buf = (u8*)&buf[0];
	msg[1].scl_rate = 200*1000;
	msg[1].udelay = 500;

	ret = i2c_transfer(client->adapter, msg, 2);   
	if (ret < 0) {
	printk("%s:err\n",__FUNCTION__);
	}

	printk("%s:b[0]=0x%x,b[1]=0x%x,b[2]=0x%x,b[3]=0x%x,b[4]=0x%x,b[5]=0x%x\n", 
		__FUNCTION__,buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);

	/*get firmware version:3bytes */	
	start_reg = 0x40;
	msg[0].addr =client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &start_reg;
	msg[0].scl_rate = 200*1000;
	msg[0].udelay = 500;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = 3;
	msg[1].buf = (u8*)&buf[0];
	msg[1].scl_rate =200*1000;
	msg[1].udelay = 500;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
	printk("%s:err\n",__FUNCTION__);
	}

	printk("%s:Ver %d.%d.%d\n",__FUNCTION__,buf[0],buf[1],buf[2]);

	return 0;
    
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

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_ABS);
	//ts->input_dev->absbit[0] = 
		//BIT(ABS_MT_POSITION_X) | BIT(ABS_MT_POSITION_Y) | 
		//BIT(ABS_MT_TOUCH_MAJOR) | BIT(ABS_MT_WIDTH_MAJOR);  // for android
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 
		    ts->x_min ? : 0,
			ts->x_max ? : 480,
			0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
			ts->y_min ? : 0,
			ts->y_max ? : 800,
			0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 1, 0, 0); //Finger Size
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 10, 0, 0); //Touch Size

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

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = ili2102_ts_early_suspend;
	ts->early_suspend.resume = ili2102_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

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
	return 0;
}

static int ili2102_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct ili2102_ts_data *ts = i2c_get_clientdata(client);
	uint8_t buf[2] = {0x30,0x30};
	struct i2c_msg msg[1];
	
	if (ts->use_irq)
	disable_irq(client->irq);
	else
	hrtimer_cancel(&ts->timer);

	ret = cancel_delayed_work_sync(&ts->work);
	if (ret && ts->use_irq) /* if work was pending disable-count is now 2 */
	enable_irq(client->irq);

	//to do suspend
	msg[0].addr =client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = buf;

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0) {
	printk("%s:err\n",__FUNCTION__);
	}
	
	DBG("%s\n",__FUNCTION__);
	
	return 0;
}

static int ili2102_ts_resume(struct i2c_client *client)
{
	struct ili2102_ts_data *ts = i2c_get_clientdata(client);
	
	//to do resume
	ili2102_init_panel(ts);
	
	if (ts->use_irq) {
		printk("enabling IRQ %d\n", client->irq);
		enable_irq(client->irq);
	}
	//else
	//hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

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
