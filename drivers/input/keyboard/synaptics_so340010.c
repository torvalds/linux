/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************************
 * driver/input/touchscreen/hannstar_Synaptics.c
 *Copyright 	:ROCKCHIP  Inc
 *Author	: 	sfm
 *Date		:  2010.2.5
 *This driver use for rk28 chip extern touchscreen. Use i2c IF ,the chip is Hannstar
 *description£º
 ********************************************************************************************/
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
#include <linux/irq.h>
#include <mach/board.h>


#define SO340010_REG_GENERAL_CONFIG 	0x0001
#define SO340010_REG_GPIO_STATE			0x0108
#define SO340010_REG_BUTTON_STATE 	0x0109
#define SO340010_REG_GPIO_CTRL			0x000E
#define SO340010_REG_GPIO_CTRL_DIR		0x0F00
#define SO340010_REG_GPIO_CTRL_DATA		0x000F
#define SO340010_REG_LED_ENABLE			0x0022
#define SO340010_REG_LED_EFFECT_PERIOD	0x0023
#define SO340010_REG_LED_CTRL1			0x0024
#define SO340010_REG_LED_CTRL1_LED0_EFFECT		0x001F
#define SO340010_REG_LED_CTRL1_LED0_BRIGHTNESS	0x0E00
#define SO340010_REG_LED_CTRL1_LED1_EFFECT		0x000E
#define SO340010_REG_LED_CTRL1_LED1_BRIGHTNESS	0x1F00
#define SO340010_REG_LED_CONTROL_2		0x0025
#define SO340010_REG_LED_CTRL2_LED2_EFFECT		0x001F
#define SO340010_REG_LED_CTRL2_LED2_BRIGHTNESS	0x0E00
#define SO340010_REG_LED_CTRL2_LED3_EFFECT		0x000E
#define SO340010_REG_LED_CTRL2_LED3_BRIGHTNESS	0x1F00
#define SO340010_REG_NUM				74
#define SO340010_IIC_SPEED		100*1000

#define PACKGE_BUFLEN		10
#define KEY0			0x1
#define KEY1 		0x2
#define KEY2 		0x4
#define KEY3 		0x8
#define KEY_ALL			(KEY0 | KEY1 | KEY2 | KEY3)
#define SYN_340010_KEY_MAXNUM		4

#if 0
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

struct so340010_kbd_info {	
	unsigned int	key_mask;	
	int 			key_code; 
};
static struct so340010_kbd_info key_table[SYN_340010_KEY_MAXNUM] = { 
	{ KEY3, KEY_BACK }, 
	{ KEY2, KEY_MENU }, 
	{ KEY1, KEY_HOME }, 
	{ KEY0, KEY_SEARCH },
};
static int key_num = sizeof(key_table)/sizeof(key_table[0]);

struct so340010_register {	
	unsigned short address; 
	const short  value;
};

static struct so340010_register so340010_register_init_table[] = {	
#if 0
	{ 0x0000,  0x0007  }, 
	{ 0x0001,  0x0020  }, 
	{ 0x0004,  0x000F  }, 
	{ 0x0010,  0xA0A0  }, 
	{ 0x0011,  0xA0A0  },
#else
	{ 0x0000,  0x0700  }, 
	{ 0x0100,  0x2000  }, 
	{ 0x0400,  0x0F00  }, 
	{ 0x1000,  0xA0A0  }, 
	{ 0x1100,  0xA0A0  },
#endif
#if (defined(CONFIG_KEYBOARD_SO340010_LED) || defined(CONFIG_KEYBOARD_SO340010_LED_FRAMEWORK))	
	{ 0x0022,  0x000f  }, 	
	{ 0x0023,  0x0000  }, 
	{ 0x0024,  0x0f0f  }, 	/* Brightness value 0 ~ 31*/	
	{ 0x0025,  0x1616  }, 
#endif
};

/*
 * Common code for bq3060 devices read
 */
static int Synaptics_touchkey_read(struct i2c_client *client, unsigned short  reg, unsigned short  buf[], unsigned len)
{
	int ret; 	
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	char tx_buf[2];
	
	tx_buf[0] = reg & 0xff;
	tx_buf[1] = (reg>>8) & 0xff;
	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = 2;
	msg.buf = (char *)tx_buf;
	msg.scl_rate = SO340010_IIC_SPEED;

	ret = i2c_transfer(adap, &msg, 1);
	if(ret < 0)
		printk("[%s]: i2c read error\n",__FUNCTION__);
	
	msg.addr = client->addr;
	msg.flags = client->flags | I2C_M_RD;
	msg.len = 2*len ;
	msg.buf = (char *)buf;
	msg.scl_rate = SO340010_IIC_SPEED;

	ret = i2c_transfer(adap, &msg, 1);
	
	//return 0;
	//ret = i2c_master_reg16_recv(client, reg, buf, len, 100*1000);
	if(ret < 0)
		printk("[%s]: i2c read error\n",__FUNCTION__);
	return ret; 
}

static int Synaptics_touchkey_write(struct i2c_client *client, unsigned short  reg, const short   buf[], unsigned len)
{

	int ret; 
	//return 0;
	ret = i2c_master_reg16_send(client, reg, buf, (int)len, 100*1000);
	if(ret < 0)
		printk("[%s]: i2c write error\n",__FUNCTION__);
	return ret;
}



struct point_data {	
	short status;	
	short x;	
	short y;
    short z;
};



struct tk_Synaptics {
	struct input_dev	*input;
	char			phys[32];
	struct delayed_work	work;
	struct workqueue_struct *wq;

	struct i2c_client	*client;
	int    g_code;
	int		gpio;
	u16			model;
	spinlock_t 	lock;
	bool		pendown;
	bool 	 	status;
	int			irq;
	int         init_flag;
	int 		has_relative_report;
	int			(*get_pendown_state)(void);
	void		(*clear_penirq)(void);
};

int Synaptics_get_pendown_state(void)
{
	return 0;
}

#if 0 
static void Synaptics_report_event(struct tk_Synaptics *tk,struct multitouch_event *tc)
{
	struct input_dev *input = tk->input;
    int cid;

    cid = tc->contactid;
    if (tk->status) {
        input_report_abs(input, ABS_X, tc->point_data[cid].x);
        input_report_abs(input, ABS_Y, tc->point_data[cid].y);
        input_sync(input);
    }
    if(tk->pendown != tk->status){
        tk->pendown = tk->status;
        input_report_key(input, BTN_TOUCH, tk->status);
        input_sync(input);
       
    }
}
#endif

static  void Synaptics_check_firmwork(struct tk_Synaptics *tk)
{
	int data=0;
    short buf[6];
	int i;
	for (i = 0; i < sizeof(so340010_register_init_table)/sizeof(so340010_register_init_table[0]); i++) {		
		if (Synaptics_touchkey_write(tk->client, so340010_register_init_table[i].address, &so340010_register_init_table[i].value, 1) < 0) {	
				printk("[%s]: config touch key error\n",__FUNCTION__);	
		}	
		DBG("[%s]: config touch key i=%d\n",__FUNCTION__,i);	
	}	
	data = Synaptics_touchkey_read(tk->client,0x0901/*0x0109*/,buf,1);
	if (data<0) {
		printk( "error reading current\n");
		return ;
	}
	DBG("Synaptics_read_values = %x\n",buf[0]);
	
	#if 0
	for (i = 0; i < sizeof(so340010_register_init_table)/sizeof(so340010_register_init_table[0]); i++) {		
		if (Synaptics_touchkey_read(tk->client, so340010_register_init_table[i].address, buf, 1) < 0) {	
				printk("[%s]: read config  touch key error\n",__FUNCTION__);	
		}	
		printk("[-->%s]: buf[0]=%x\n",__FUNCTION__,buf[0]);
	}
	
	if ( Synaptics_touchkey_read(tk->client, SO340010_REG_BUTTON_STATE, buf,1) < 0) 	
		printk("[%s]: config touch key error\n",__FUNCTION__);	
	
	printk("[-->%s]: buf[0]=%x buf[1]=%x\n",__FUNCTION__,buf[0],buf[1]);
	#endif
}



static inline int Synaptics_read_values(struct tk_Synaptics *tk)  ///, struct multitouch_event *tc)
{
	int data=0;
	short buf;
	int i;
	data = Synaptics_touchkey_read(tk->client,0X0B01/*0x010b*/,&buf,1);
	if (data<0) {
		printk( "error reading current\n");
		return 0;
	}
	DBG("Synaptics_read_values = %x\n",buf);
	
	data = Synaptics_touchkey_read(tk->client,0X0901/*0x0108*/,&buf,1);
	if (data<0) {
		printk( "error reading current\n");
		return 0;
	}
	DBG("Synaptics_read_values = %x\n",buf);
	buf = buf>>8;
	if(buf == 0)
		goto exit_ret;
	for(i=0;i<SYN_340010_KEY_MAXNUM;i++){
		if(buf == key_table[i].key_mask)
			break;
	}
	tk->g_code = key_table[i].key_code;
	input_report_key(tk->input, tk->g_code, 1);
	input_sync(tk->input);
	input_report_key(tk->input, tk->g_code, 0);
	input_sync(tk->input);	
exit_ret:	
	if(!gpio_get_value(tk->gpio)){   	
    	Synaptics_check_firmwork(tk);
    }
    return 10;
    	
}


static void Synaptics_work(struct work_struct *work)
{
	struct tk_Synaptics *tk =
		container_of(to_delayed_work(work), struct tk_Synaptics, work);
	//struct multitouch_event *tc = &tk->mt_event;
	DBG("Enter:%s %d\n",__FUNCTION__,__LINE__);
	if(tk->init_flag == 1){
		tk->init_flag = 0;
		Synaptics_check_firmwork(tk);
		return;	
	}
		
	if( Synaptics_read_values(tk)<0)  //,tc)<0)
	{
		printk("-->%s Synaptics_read_values error  line=%d\n",__FUNCTION__,__LINE__);
		goto out ;
	}
    	//Synaptics_report_event(tk,tc);
out:               
	//if (tk->pendown){
	//	schedule_delayed_work(&tk->work, msecs_to_jiffies(8));
	//	tk->pendown = 0;
	//}
	//else{
		enable_irq(tk->irq);
	//}

}


static irqreturn_t Synaptics_irq(int irq, void *handle)
{
	struct tk_Synaptics *tk = handle;
	unsigned long flags;
	DBG("Enter:%s %d\n",__FUNCTION__,__LINE__);
	spin_lock_irqsave(&tk->lock,flags);
	if (!tk->get_pendown_state || likely(tk->get_pendown_state())) {
		disable_irq_nosync(tk->irq);
		schedule_delayed_work(&tk->work,msecs_to_jiffies(20));
	}
	spin_unlock_irqrestore(&tk->lock,flags);
	return IRQ_HANDLED;
}

static void Synaptics_free_irq(struct tk_Synaptics *tk)
{
	free_irq(tk->irq, tk);
	if (cancel_delayed_work_sync(&tk->work)) {
		/*
		 * Work was pending, therefore we need to enable
		 * IRQ here to balance the disable_irq() done in the
		 * interrupt handler.
		 */
		enable_irq(tk->irq);
	}
}

static int __devinit synaptics_touchkey_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct tk_Synaptics *tk;
	struct synaptics_platform_data *pdata = pdata = client->dev.platform_data;
	struct input_dev *input_dev;
	int err,i;
	//short reg,buff;	
	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;
	tk = kzalloc(sizeof(struct tk_Synaptics), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!tk || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}
	tk->client = client;
	tk->irq = client->irq;
	tk->input = input_dev;
	///tk->init_flag = 1;
	//tk->wq = create_rt_workqueue("Synaptics_wq");
	INIT_DELAYED_WORK(&tk->work, Synaptics_work);
	snprintf(tk->phys, sizeof(tk->phys), "%s/input0", dev_name(&client->dev));
	input_dev->name = "synaptics_touchkey";
	input_dev->phys = tk->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->evbit[0] = BIT_MASK(EV_ABS)|BIT_MASK(EV_KEY)|BIT_MASK(EV_SYN);
	set_bit(EV_KEY, input_dev->evbit);
	for (i = 0; i < key_num; i++) {		
		set_bit(key_table[i].key_code,input_dev->keybit);	
	}
	if (pdata->init_platform_hw)
		pdata->init_platform_hw();
	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;
	i2c_set_clientdata(client, tk);
	
	//reg = 0x3000;
	//buff = 0x0001;
	//Synaptics_touchkey_write(client,reg,&buff, 2);
	//mdelay(500);
	Synaptics_check_firmwork(tk);
	///schedule_delayed_work(&tk->work,msecs_to_jiffies(8*1000));
	tk->gpio = tk->irq;
	if (!tk->irq) {
		dev_dbg(&tk->client->dev, "no IRQ?\n");
		return -ENODEV;
	}else{
		tk->irq = gpio_to_irq(tk->irq);
	}
	err = request_irq(tk->irq, Synaptics_irq, IRQF_TRIGGER_FALLING,client->dev.driver->name, tk);
	if (err < 0) {
		dev_err(&client->dev, "irq %d busy?\n", tk->irq);
		goto err_free_mem;
	}
	if (err < 0)
		goto err_free_irq;
	return 0;
 err_free_irq:
	Synaptics_free_irq(tk);
	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();
 err_free_mem:
	input_free_device(input_dev);
	kfree(tk);
	return err;
}

static int __devexit Synaptics_remove(struct i2c_client *client)
{
	struct tk_Synaptics *tk = i2c_get_clientdata(client);
	struct synaptics_platform_data *pdata = client->dev.platform_data;

	Synaptics_free_irq(tk);

	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();

	input_unregister_device(tk->input);
	kfree(tk);

	return 0;
}

static struct i2c_device_id synaptics_touchkey_idtable[] = {
	{ "synaptics_touchkey", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, sintek_idtable);

static struct i2c_driver synaptics_touchkey_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "synaptics_touchkey"
	},
	.id_table	= synaptics_touchkey_idtable,
	.probe		= synaptics_touchkey_probe,
	.remove		= __devexit_p(Synaptics_remove),
};

static void __init synaptics_touchkey_init_async(void *unused, async_cookie_t cookie)
{
	DBG("--------> %s <-------------\n",__func__);
	i2c_add_driver(&synaptics_touchkey_driver);
}

static int __init synaptics_touchkey_init(void)
{
	async_schedule(synaptics_touchkey_init_async, NULL);
	return 0;
}

static void __exit synaptics_touchkey_exit(void)
{
	return i2c_del_driver(&synaptics_touchkey_driver);
}
module_init(synaptics_touchkey_init);
module_exit(synaptics_touchkey_exit);
MODULE_LICENSE("GPL");

