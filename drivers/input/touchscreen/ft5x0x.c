#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/irq.h>
#include <mach/board.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/input/mt.h>
#include <linux/regulator/rk29-pwm-regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>

//#define FT5X0X_DEBUG
#ifdef FT5X0X_DEBUG
#define DBG(fmt, args...)	printk("*** " fmt, ## args)
#else
#define DBG(fmt, args...)	do{}while(0)
#endif

#define EV_MENU					KEY_MENU

#define I2C_SPEED 200*1000
#define MAX_POINT  5

#if defined (CONFIG_TOUCHSCREEN_1024X768)
#define SCREEN_MAX_X 1024
#define SCREEN_MAX_Y 768
#elif defined (CONFIG_TOUCHSCREEN_1024X600)
#define SCREEN_MAX_X 1024
#define SCREEN_MAX_Y 600
#elif defined (CONFIG_TOUCHSCREEN_800X600)
#define SCREEN_MAX_X 800
#define SCREEN_MAX_Y 600
#elif defined (CONFIG_TOUCHSCREEN_800X480)
#define SCREEN_MAX_X 800
#define SCREEN_MAX_Y 480
#else
#define SCREEN_MAX_X 800
#define SCREEN_MAX_Y 480
#endif

#define PRESS_MAX 200

#define VID_OF		0x51	//OuFei
#define VID_MD		0x53	//MuDong
#define VID_BYD		0x59
#define VID_BM		0x5D	//BaoMing
#define VID_YJ		0x80
#define VID_DSW		0x8C	//DingShengWei
#define VID_YM		0x94    //0xC0
static unsigned char g_vid;

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend ft5x0x_early_suspend;
#endif

#ifndef TOUCH_EN_LEVEL
#define TOUCH_EN_LEVEL GPIO_HIGH
#endif

static int  ft5x0x_probe(struct i2c_client *client, const struct i2c_device_id *id);

struct ts_event {
    u16    flag;
    u16    x;
    u16    y;
    u16    pressure;
    u16    w;
};
static struct ts_event ts_point[MAX_POINT];

struct ft5x0x_data
{
	struct i2c_client *client;
	struct input_dev	*input_dev;
	int		reset_gpio;
	int		touch_en_gpio;
	int		last_point_num;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
};

struct i2c_client *g_client;

/***********************************************************************************************
Name	:	ft5x0x_i2c_rxdata 

Input	:	*rxdata
                     *length

Output	:	ret

function	:	

***********************************************************************************************/
int ft5x0x_i2c_Read(char * writebuf, int writelen, char *readbuf, int readlen)
{
	int ret;

	if(writelen > 0)
	{
		struct i2c_msg msgs[] = {
			{
				.addr	= g_client->addr,
				.flags	= 0,
				.len	= writelen,
				.buf	= writebuf,
				.scl_rate = I2C_SPEED,
			},
			{
				.addr	= g_client->addr,
				.flags	= I2C_M_RD,
				.len	= readlen,
				.buf	= readbuf,
				.scl_rate = I2C_SPEED,
			},
		};
		ret = i2c_transfer(g_client->adapter, msgs, 2);
		if (ret < 0)
			DBG("msg %s i2c read error: %d\n", __func__, ret);
	}
	else
	{
		struct i2c_msg msgs[] = {
			{
				.addr	= g_client->addr,
				.flags	= I2C_M_RD,
				.len	= readlen,
				.buf	= readbuf,
				.scl_rate = I2C_SPEED,
			},
		};
		ret = i2c_transfer(g_client->adapter, msgs, 1);
		if (ret < 0)
			DBG("msg %s i2c read error: %d\n", __func__, ret);
	}
	return ret;
}EXPORT_SYMBOL(ft5x0x_i2c_Read);
/***********************************************************************************************
Name	:	 ft5x0x_i2c_Write

Input	:	
                     

Output	:0-write success 	
		other-error code	
function	:	write data by i2c 

***********************************************************************************************/
int ft5x0x_i2c_Write(char *writebuf, int writelen)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= g_client->addr,
			.flags	= 0,
			.len	= writelen,
			.buf	= writebuf,
			.scl_rate = I2C_SPEED,
		},
	};

	ret = i2c_transfer(g_client->adapter, msg, 1);
	if (ret < 0)
		DBG("%s i2c write error: %d\n", __func__, ret);

	return ret;
}EXPORT_SYMBOL(ft5x0x_i2c_Write);

int ft5x0x_rx_data(struct i2c_client *client, char *rxData, int length)
{
	int ret = 0;
	char reg = rxData[0];
	ret = i2c_master_reg8_recv(client, reg, rxData, length, I2C_SPEED);
	return (ret > 0)? 0 : ret;
}

static int ft5x0x_tx_data(struct i2c_client *client, char *txData, int length)
{
	int ret = 0;
	char reg = txData[0];
	ret = i2c_master_reg8_send(client, reg, &txData[1], length-1, I2C_SPEED);
	return (ret > 0)? 0 : ret;
}

char ft5x0x_read_reg(struct i2c_client *client, int addr)
{
	char tmp;
	int ret = 0;

	tmp = addr;
	ret = ft5x0x_rx_data(client, &tmp, 1);
	if (ret < 0) {
		return ret;
	}
	return tmp;
}

int ft5x0x_write_reg(struct i2c_client *client,int addr,int value)
{
	char buffer[3];
	int ret = 0;

	buffer[0] = addr;
	buffer[1] = value;
	ret = ft5x0x_tx_data(client, &buffer[0], 2);
	return ret;
}

static void ft5x0x_power_en(struct ft5x0x_data *tsdata, int on)
{
#if defined (TOUCH_POWER_PIN)
	if (on) {
		gpio_direction_output(tsdata->touch_en_gpio, TOUCH_EN_LEVEL);
		gpio_set_value(tsdata->touch_en_gpio, TOUCH_EN_LEVEL);
		mdelay(10);
	} else {
		gpio_direction_output(tsdata->touch_en_gpio, !TOUCH_EN_LEVEL);
		gpio_set_value(tsdata->touch_en_gpio, !TOUCH_EN_LEVEL);
		mdelay(10);
	}
#endif
}

static void ft5x0x_chip_reset(struct ft5x0x_data *tsdata)
{
    gpio_direction_output(tsdata->reset_gpio, 0);
    gpio_set_value(tsdata->reset_gpio, 1);
	mdelay(20);
    gpio_set_value(tsdata->reset_gpio, 0);
	mdelay(20);
    gpio_set_value(tsdata->reset_gpio, 1);
}

static int i2c_write_interface(unsigned char* pbt_buf, int dw_lenth)
{
    int ret;
    ret = i2c_master_send(g_client, pbt_buf, dw_lenth);
    if (ret <= 0) {
        printk("i2c_write_interface error\n");
        return -1;
    }

    return 0;
}

static int ft_cmd_write(unsigned char btcmd, unsigned char btPara1, unsigned char btPara2,
		unsigned char btPara3, int num)
{
    unsigned char write_cmd[4] = {0};

    write_cmd[0] = btcmd;
    write_cmd[1] = btPara1;
    write_cmd[2] = btPara2;
    write_cmd[3] = btPara3;
    return i2c_write_interface(&write_cmd, num);
}


static int ft5x0x_chip_init(struct i2c_client * client)
{
	int ret = 0;
	int w_value;
	char r_value;
	int err = -1;
	int reg;
	int i = 0, flag = 1;
	struct ft5x0x_data *tsdata = i2c_get_clientdata(client);

	gpio_free(tsdata->reset_gpio);
	err = gpio_request(tsdata->reset_gpio, "ft5x0x rst");
	if (err) {
		DBG( "failed to request ft5x0x reset GPIO%d\n", tsdata->reset_gpio);
		goto exit_alloc_gpio_rst_failed;
	}
	
#if defined (TOUCH_POWER_PIN)
#if defined (TOUCH_POWER_MUX_NAME)
    rk29_mux_api_set(TOUCH_POWER_MUX_NAME, TOUCH_POWER_MUX_MODE_GPIO);
#endif
	gpio_free(tsdata->touch_en_gpio);
	err = gpio_request(tsdata->touch_en_gpio, "ft5x0x power enable");
	if (err) {
		DBG( "failed to request ft5x0x power enable GPIO%d\n", tsdata->touch_en_gpio);
		goto exit_alloc_gpio_power_failed;
	}
#endif

	ft5x0x_power_en(tsdata, 0);
	mdelay(100);
	ft5x0x_chip_reset(tsdata);
	ft5x0x_power_en(tsdata, 1);
	mdelay(500);
    ft_cmd_write(0x07,0x00,0x00,0x00,1);
	mdelay(10);

#if 1
	while (1) {
		reg = 0x88;
		w_value = 7; 
		ret = ft5x0x_write_reg(client, reg, w_value);    /* adjust frequency 70Hz */
		if (ret < 0) {
			printk(KERN_ERR "ft5x0x i2c txdata failed\n");
			//goto out;
		}

		r_value = ft5x0x_read_reg(client, reg);
		if (ret < 0) {
			printk(KERN_ERR "ft5x0x i2c rxdata failed\n");
			//goto out;
		}
		printk("r_value = %d\n, i = %d, flag = %d", r_value, i, flag);
		i++;

		if (w_value != r_value) {
			ret = -1;
			flag = 0;
			if (i > 10) { /* test 5 count */
				break;
			}
		} else {
			ret = 0;
			break;
		}
	}

      if( ret == -1)
          return ret;

#endif
	ret = ft5x0x_read_reg(client, 0xA8);//read touchpad ID for adjust touchkey place
	if (ret < 0) {
		printk(KERN_ERR "ft5x0x i2c rxdata failed\n");
		//goto out;
	}
	printk("ft5406 g_vid = 0x%X\n", ret);
	g_vid = ret;

	return ret;

exit_alloc_gpio_power_failed:
#if defined (TOUCH_POWER_PIN)
	gpio_free(tsdata->touch_en_gpio);
#endif
exit_alloc_gpio_rst_failed:
    gpio_free(tsdata->reset_gpio);
	printk("%s error\n",__FUNCTION__);
	return err;
}

static void key_led_ctrl(int on)
{
#ifdef TOUCH_KEY_LED
	gpio_set_value(TOUCH_KEY_LED, on);
#endif
}

static int g_screen_key=0;

static int ft5x0x_process_points(struct ft5x0x_data *data)
{
	struct i2c_client *client = data->client;
	u8 start_reg = 0x0;
	u8 buf[32] = {0};
	int ret = -1;
	int status = 0, id, x, y, p, w, touch_num;
	int offset, i;
	int back_press = 0, search_press=0, menu_press=0, home_press=0;
	int points;

	start_reg = 0;
	buf[0] = start_reg;

	//printk("ft5406 g_vid = 0x%X\n", g_vid);
	if (MAX_POINT == 5) {
		ret = ft5x0x_rx_data(client, buf, 31);
	} else {
		ret = ft5x0x_rx_data(client, buf, 13);
	}

    if (ret < 0) {
		printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}


	if (MAX_POINT == 5) {
		touch_num = buf[2] & 0x07;
	} else {
		touch_num = buf[2] & 0x03;
	}

	if (touch_num == 0) {
		for (i = 0; i < MAX_POINT; i++) {
			input_mt_slot(data->input_dev, i);
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, false);
		}

		input_sync(data->input_dev);
		DBG("release all points!!!!!!\n");
		return 0;
	}

	points = touch_num;
	if (data->last_point_num > touch_num) {
		touch_num = data->last_point_num;
	}
	data->last_point_num = points;

	offset = 0;
    for (i = 0; i < touch_num; i++) {        
		id = buf[5 + offset] >> 4;
        status = buf[3  + offset] >> 6;
        x = (s16)(buf[3 + offset] & 0x0F) << 8 | (s16)buf[4 + offset];
        y = (s16)(buf[5 + offset] & 0x0F) << 8 | (s16)buf[6 + offset];

        //p = buf[7 + offset];
        //w = buf[8 + offset];

        offset += 6;
		
        //printk("%d-%d(%d,%d)%d-%d\n", id, status, x, y, p, w);
		DBG("TOUCH_NO=%d: ID=%d,(X=%d,Y=%d), status=%d, pressure=%d, w=%d\n", i, id, x, y, status, 0, 0);

		if (x < (SCREEN_MAX_X + 10)) {
			if (status == 1) {
				input_mt_slot(data->input_dev, id);
				input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, false);
			} else {
				input_mt_slot(data->input_dev, id);
				input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);
				input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 200);
				input_report_abs(data->input_dev, ABS_MT_POSITION_X, x);
				input_report_abs(data->input_dev, ABS_MT_POSITION_Y, y);
			}
		} else {
		}
	}

	input_sync(data->input_dev);

	return 0;
}

static void  ft5x0x_delaywork_func(struct work_struct *work)
{
	struct ft5x0x_data *ft5x0x = container_of(work, struct ft5x0x_data, pen_event_work);
	struct i2c_client *client = ft5x0x->client;

	ft5x0x_process_points(ft5x0x);
	enable_irq(client->irq);		
}

static irqreturn_t ft5x0x_interrupt(int irq, void *handle)
{
	struct ft5x0x_data *ft5x0x_ts = handle;

	//printk("Enter:%s %d\n",__FUNCTION__,__LINE__);
	disable_irq_nosync(irq);
	//if (!work_pending(&ft5x0x_ts->pen_event_work)) {
		queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
	//}
	return IRQ_HANDLED;
}


static int ft5x0x_remove(struct i2c_client *client)
{
	struct ft5x0x_data *ft5x0x = i2c_get_clientdata(client);
	
    input_unregister_device(ft5x0x->input_dev);
    input_free_device(ft5x0x->input_dev);
    free_irq(client->irq, ft5x0x);
    kfree(ft5x0x); 
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ft5x0x_early_suspend);
#endif      
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ft5x0x_suspend(struct early_suspend *h)
{
	int err;
	int w_value;
	int reg;
	struct ft5x0x_data *ft5x0x = i2c_get_clientdata(g_client);

	printk("==ft5x0x_ts_suspend=\n");
	key_led_ctrl(0);

#if 1
	w_value = 3;
	reg = 0xa5;
	err = ft5x0x_write_reg(g_client, reg, w_value);   /* enter sleep mode */
	if (err < 0) {
		printk("ft5x0x enter sleep mode failed\n");
	}
#endif
	disable_irq(g_client->irq);		
	//ft5x0x_power_en(ft5x0x, 0);
}

static void ft5x0x_resume(struct early_suspend *h)
{
	struct ft5x0x_data *ft5x0x = i2c_get_clientdata(g_client);

	key_led_ctrl(0);

	printk("==ft5x0x_ts_resume=\n");
	//ft5x0x_power_en(ft5x0x, 1);
	ft5x0x_chip_reset(ft5x0x);

	mdelay(100);

	enable_irq(g_client->irq);		
}
#else
static int ft5x0x_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return 0;
}
static int ft5x0x_resume(struct i2c_client *client)
{
	return 0;
}
#endif

static const struct i2c_device_id ft5x0x_id[] = {
		{"ft5x0x_ts", 0},
		{ }
};

MODULE_DEVICE_TABLE(i2c, ft5x0x_id);

static struct i2c_driver ft5x0x_driver = {
	.driver = {
		.name = "ft5x0x_ts",
	    },
	.id_table 	= ft5x0x_id,
	.probe		= ft5x0x_probe,
	.remove		= __devexit_p(ft5x0x_remove),
#ifndef CONFIG_HAS_EARLYSUSPEND	
	.suspend = &ft5x0x_suspend,
	.resume = &ft5x0x_resume,
#endif	
};

static int ft5x0x_client_init(struct i2c_client *client)
{
	struct ft5x0x_data *tsdata = i2c_get_clientdata(client);
	int ret = 0;

	DBG("gpio_to_irq(%d) is %d\n", client->irq, gpio_to_irq(client->irq));
	if ( !gpio_is_valid(client->irq)) {
		DBG("+++++++++++gpio_is_invalid\n");
		return -EINVAL;
	}

	gpio_free(client->irq);
	ret = gpio_request(client->irq, "ft5x0x_int");
	if (ret) {
		DBG( "failed to request ft5x0x GPIO%d\n", gpio_to_irq(client->irq));
		return ret;
	}

    ret = gpio_direction_input(client->irq);
    if (ret) {
        DBG("failed to set ft5x0x gpio input\n");
		return ret;
    }

	gpio_pull_updown(client->irq, GPIOPullUp);
	client->irq = gpio_to_irq(client->irq);
	//ft5x0x->irq = client->irq;
	ret = request_irq(client->irq, ft5x0x_interrupt, IRQF_TRIGGER_FALLING, client->dev.driver->name, tsdata);
	DBG("request irq is %d,ret is 0x%x\n", client->irq, ret);
	if (ret ) {
		DBG(KERN_ERR "ft5x0x_client_init: request irq failed,ret is %d\n", ret);
        return ret;
	}
	//disable_irq(client->irq);

	return 0;
}

static int  ft5x0x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ft5x0x_data *ft5x0x_ts;
	struct ts_hw_data *pdata = client->dev.platform_data;
	int err = 0;
	int i;

	printk("%s enter\n",__FUNCTION__);
	ft5x0x_ts = kzalloc(sizeof(struct ft5x0x_data), GFP_KERNEL);
	if (!ft5x0x_ts) {
		DBG("[ft5x0x]:alloc data failed.\n");
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}
    
    memset(ts_point, 0x0, sizeof(struct ts_event) * MAX_POINT);

	g_client = client;
	ft5x0x_ts->client = client;
	ft5x0x_ts->last_point_num = 0;
	ft5x0x_ts->reset_gpio = pdata->reset_gpio;
	ft5x0x_ts->touch_en_gpio = pdata->touch_en_gpio;
	i2c_set_clientdata(client, ft5x0x_ts);

	err = ft5x0x_chip_init(client);
	if (err < 0) {
		printk(KERN_ERR
		       "ft5x0x_probe: ft5x0x chip init failed\n");
		goto exit_request_gpio_irq_failed;
	}

	err = ft5x0x_client_init(client);
	if (err < 0) {
		printk(KERN_ERR
		       "ft5x0x_probe: ft5x0x_client_init failed\n");
		goto exit_request_gpio_irq_failed;
	}
		
	ft5x0x_ts->input_dev = input_allocate_device();
	if (!ft5x0x_ts->input_dev) {
		err = -ENOMEM;
		printk(KERN_ERR
		       "ft5x0x_probe: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}

	ft5x0x_ts->input_dev->name = "ft5x0x-ts";
	ft5x0x_ts->input_dev->dev.parent = &client->dev;

	err = input_register_device(ft5x0x_ts->input_dev);
	if (err < 0) {
		printk(KERN_ERR
		       "ft5x0x_probe: Unable to register input device: %s\n",
		       ft5x0x_ts->input_dev->name);
		goto exit_input_register_device_failed;
	}

	INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_delaywork_func);
	ft5x0x_ts->ts_workqueue = create_singlethread_workqueue("ft5x0x_ts");
	if (!ft5x0x_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_request_gpio_irq_failed;
	}


	__set_bit(EV_SYN, ft5x0x_ts->input_dev->evbit);
	__set_bit(EV_KEY, ft5x0x_ts->input_dev->evbit);
	__set_bit(EV_ABS, ft5x0x_ts->input_dev->evbit);
	__set_bit(INPUT_PROP_DIRECT, ft5x0x_ts->input_dev->propbit);

	input_mt_init_slots(ft5x0x_ts->input_dev, MAX_POINT);
	input_set_abs_params(ft5x0x_ts->input_dev, ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(ft5x0x_ts->input_dev, ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(ft5x0x_ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);

#ifdef CONFIG_HAS_EARLYSUSPEND
    ft5x0x_early_suspend.suspend = ft5x0x_suspend;
    ft5x0x_early_suspend.resume = ft5x0x_resume;
    ft5x0x_early_suspend.level = 0x2;
    register_early_suspend(&ft5x0x_early_suspend);
#endif

	return 0;

exit_input_register_device_failed:
	input_free_device(ft5x0x_ts->input_dev);
exit_input_allocate_device_failed:
    free_irq(client->irq, ft5x0x_ts);
exit_request_gpio_irq_failed:
	kfree(ft5x0x_ts);	
exit_alloc_gpio_power_failed:
#if defined (TOUCH_POWER_PIN)
	gpio_free(ft5x0x_ts->touch_en_gpio);
#endif
exit_alloc_gpio_rst_failed:
    gpio_free(ft5x0x_ts->reset_gpio);
exit_alloc_data_failed:
	printk("%s error\n",__FUNCTION__);
	return err;
}

static void __init ft5x0x_init_async(void *unused, async_cookie_t cookie)
{
	i2c_add_driver(&ft5x0x_driver);
}

static int __init ft5x0x_mod_init(void)
{
	printk("ft5x0x module init\n");
	async_schedule(ft5x0x_init_async, NULL);
	return 0;
}

static void __exit ft5x0x_mod_exit(void)
{
	i2c_del_driver(&ft5x0x_driver);
}

module_init(ft5x0x_mod_init);
module_exit(ft5x0x_mod_exit);

MODULE_DESCRIPTION("ft5406 touch driver");
MODULE_AUTHOR("zqqu<zqqu@yifangdigital.com>");
MODULE_LICENSE("GPL");

