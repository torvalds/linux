/* drivers/i2c/chips/ntp070.c - ntp070 compass driver
 *
 * Copyright (C) 2007-2008 HTC Corporation.
 * Author: Hou-Kun Chen <houkun.chen@gmail.com>
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

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_ANDROID_POWER
#include <linux/android_power.h>
#endif

//#define RK28_PRINT
//#include <asm/arch/rk28_debug.h>
#if 1
#define rk28printk(x...) printk(x)
#else
#define rk28printk(x...)
#endif

#define LCD_MAX_LENGTH				800
#define LCD_MAX_WIDTH				480

struct ntp070_data{	
	struct input_dev *input_dev;
	struct i2c_client *client;
	int 	irq;
	struct work_struct work;	
};

#define TP_STATE_IDLE         0
#define TP_STATE_DOWN         1

unsigned char tp_state = TP_STATE_IDLE;
static bool irq_finished = true;

static int  ntp070_probe(struct i2c_client *client, const struct i2c_device_id *id);


/* Addresses to scan -- protected by sense_data_mutex */
//static char sense_data[RBUFF_SIZE + 1];
static struct i2c_client *this_client;
struct ntp070_data *g_ts_dev = NULL;

#ifdef CONFIG_ANDROID_POWER
static android_early_suspend_t ntp070_early_suspend;
#endif
static int revision = -1;

//#define TWO_TOUCH_POINT 
#define TOUCH_TP20466A //  ±ÈÑÇµÏ

#ifdef TOUCH_TP20466A
#define TOUCH_SPEED		UL(250 * 1000)
#define TOUCHDATA_LEN    (34)
#define TP_MAX_WIDTH				(0x1dba)//7610
#define TP_MAX_LENGTH				(0x12cc)//4812
#define TP_X0  (0xAA)// 170
#define TP_Y0  (0x0E)// 14
static u8 tp20466a_init_data[] =
{
	0x19,0x05,0x06,0x28,0x02,0x14,0x14,0x10,0x28,0xB0,0x14,0x00,0x1E,0x00,0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xE1,0x00,0x00,0x00,0x00,0x4D,0xCF,0x20,0x03,0x03,0x83,0x50,0x3C,0x1E,0xB4,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
};
/* write a array of registers  */
//static int TP20466A_write_array(struct i2c_client *client, struct reginfo *regarray)
static int tp20466a_i2c_set_regs(struct i2c_client *client, u8 reg, u8 const buf[], __u16 len)//hym8563_i2c_set_regs
{
	int ret;
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, TOUCH_SPEED);
	return ret;
}
#else
#define TOUCH_SPEED		UL(400 * 1000)//
#define TP_MAX_WIDTH				(1266)
#define TP_MAX_LENGTH				(766)
#define TOUCHDATA_LEN    (26)
#endif

/* sensor register read */
static int ntp070_read(struct i2c_client *client, u8 reg, u8 *val)//sensor_read
{
    int err,cnt,i;
    u8 buf[TOUCHDATA_LEN];
    struct i2c_msg msg[1];

    buf[0] = reg & 0xFF;

    msg->addr = client->addr;
    msg->flags = client->flags;
    msg->buf = buf;
    msg->len = 1; //zfl sizeof(buf);
    msg->scl_rate = TOUCH_SPEED;//40*1000;                                        /* ddl@rock-chips.com : 100kHz */
    i2c_transfer(client->adapter, msg, 1);
    msg->addr = client->addr;
    msg->flags = client->flags|I2C_M_RD;
    msg->buf = buf;
    msg->len = TOUCHDATA_LEN; //zfl 1;                                     /* ddl@rock-chips.com : 100kHz */

    cnt = 2;
    err = -EAGAIN;
    while ((cnt--) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 1);

        if (err >= 0) {
            //zfl *val = buf[0];
            for(i=0;i<TOUCHDATA_LEN;i++)*(val+i) = buf[i];
            return 0;
        } else {
        	printk("\n ntp070_read write reg failed, try to read again!\n");
            udelay(10);
         }
    }

    return err;
}
#if 0 
static int tp20466a_read_test(struct i2c_client *client, u8 reg, u8 *val)//NO
{
    int err,cnt,i;
    u8 buf[TOUCHDATA_LEN];
    struct i2c_msg msg[1];

    buf[0] = reg & 0xFF;

    msg->addr = client->addr;
    msg->flags = client->flags;
    msg->buf = buf;
    msg->len = 1; //zfl sizeof(buf);
    msg->scl_rate = TOUCH_SPEED;//40*1000;                                        /* ddl@rock-chips.com : 100kHz */
    i2c_transfer(client->adapter, msg, 1);
    msg->addr = client->addr;
    msg->flags = client->flags|I2C_M_RD;
    msg->buf = buf;
    msg->len = 1; //zfl 1;                                     /* ddl@rock-chips.com : 100kHz */

    cnt = 5;
    err = -EAGAIN;
    while ((cnt--) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 1);

        if (err >= 0) {
            //*val = buf[0];
            //for(i=0;i<1;i++)*(val) = buf[i];
        	printk("\n tp20466a_read_test read =0x%x\n",buf[0]);
            return 0;
        } else {
        	printk("\n tp20466a_read_test write reg failed, try to read again!\n");
            udelay(10);
         }
    }

    return err;
}
#endif
static void ntp070_work_func(struct work_struct *work)
{
	struct ntp070_data *ts_dev = g_ts_dev;
	int ak4183_irq_pin_level = 0;
	int ak4183_input_report_flag = 0;
	int xpos, ypos,points,pressure_p0,pressure_p1;
	int ret = 0;	
	unsigned char buf[TOUCHDATA_LEN];
	int x2pos,y2pos,temp;
	int tmpx,tmpy;

#ifdef TOUCH_TP20466A
	ret = ntp070_read(this_client,0x00,buf);
	//if(ret < 0)	goto fake_touch;
	points = buf[0]&0x01 + (buf[0]>>1)&0x01 + (buf[0]>>2)&0x01 + (buf[0]>>3)&0x01 + (buf[0]>>4)&0x01 ;
	ypos = ((unsigned int)buf[2])<<8|(unsigned int)buf[3];
	xpos = ((unsigned int)buf[4])<<8|(unsigned int)buf[5];
	tmpx=xpos;tmpy=ypos;
	pressure_p0 = buf[6];
	if(pressure_p0<2)points=0;
	y2pos = ((unsigned int)buf[7])<<8|(unsigned int)buf[8];
	x2pos = ((unsigned int)buf[9])<<8|(unsigned int)buf[10];
	pressure_p1 = buf[11];
	if(ypos>TP_MAX_LENGTH) { ypos=TP_MAX_LENGTH;points=0;}
	else if(ypos<TP_Y0){ ypos=TP_Y0;points=0;}
	ypos-=TP_Y0;
	ypos=(ypos*(LCD_MAX_WIDTH-1))/(TP_MAX_LENGTH-TP_Y0);ypos=(LCD_MAX_WIDTH-1)-ypos;
	if(xpos>TP_MAX_WIDTH) {xpos=TP_MAX_WIDTH;points=0;}
	else if(xpos<TP_X0) {xpos=TP_X0;points=0;}
	xpos-=TP_X0;
	xpos=(xpos*(LCD_MAX_LENGTH-1))/(TP_MAX_WIDTH-TP_X0);
	rk28printk("buf[0x]=%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x\n",buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9],buf[10],buf[11]);
	rk28printk("tmpx=0x%x,tmpy=0x%x,press0=%d,press1=%d\n",tmpx,tmpy,pressure_p0,pressure_p1);
#else
	//zfl ret = i2c_master_reg8_recv(this_client,0xF9, buf, 26, TOUCH_SPEED);
	ret = ntp070_read(this_client,0xF9,buf);
	//if(ret < 0)	goto fake_touch;
	ypos = ((unsigned int)buf[5])<<8|(unsigned int)buf[6];
	xpos = ((unsigned int)buf[7])<<8|(unsigned int)buf[8];
	//temp = xpos;//
	//xpos = ypos*800/1280;//
	//ypos = (768 - temp)*480/768;//*/
	y2pos = ((unsigned int)buf[9])<<8|(unsigned int)buf[10];
	x2pos = ((unsigned int)buf[11])<<8|(unsigned int)buf[12];
	points=buf[3];

	if(ypos>TP_MAX_LENGTH) ypos=0;
	else ypos=TP_MAX_LENGTH-ypos;
	ypos=(ypos*(LCD_MAX_WIDTH-1))/TP_MAX_LENGTH;
	if(xpos>TP_MAX_WIDTH) xpos=TP_MAX_WIDTH;
	xpos=(xpos*(LCD_MAX_LENGTH-1))/TP_MAX_WIDTH;
#endif
	if(ret < 0){
		goto fake_touch;
	}
	if(0==points)
	{
		ak4183_irq_pin_level = 1;
		goto fake_touch;
	}
	if(tp_state == TP_STATE_IDLE){
		input_report_key(ts_dev->input_dev, BTN_TOUCH, 1);
		ak4183_input_report_flag = 1;
		tp_state = TP_STATE_DOWN;
	}
		
	if(tp_state == TP_STATE_DOWN){
		input_report_abs(ts_dev->input_dev, ABS_X, xpos);
		input_report_abs(ts_dev->input_dev, ABS_Y, ypos);
#ifdef TOUCH_TP20466A
		//input_report_abs(ts_dev->input_dev, ABS_PRESSURE, pressure_p0);
#endif
		ak4183_input_report_flag = 1;
	}

fake_touch:
	if(ak4183_irq_pin_level == 1 && tp_state == TP_STATE_DOWN){
		input_report_key(ts_dev->input_dev, BTN_TOUCH, 0);
		ak4183_input_report_flag = 1;
		tp_state = TP_STATE_IDLE;
	}

	if(ak4183_input_report_flag != 0)
		input_sync(ts_dev->input_dev);
	rk28printk("%s-point:%d,x:%d,y:%d,x2:%d,y2:%d,tp_state:%d,flag:%d\n\n",__FUNCTION__,points,xpos,ypos,x2pos,y2pos,tp_state,ak4183_input_report_flag);
	enable_irq(ts_dev->irq);
	irq_finished = true;
}

static irqreturn_t ntp070_interrupt(int irq, void *dev_id)
{
	struct ntp070_data *ts_dev = (struct ntp070_data *)dev_id;

	if(!irq_finished)
		return IRQ_HANDLED;
	irq_finished = false;
	disable_irq(ts_dev->irq);
	schedule_work(&ts_dev->work);
	rk28printk("%s:%d\n",__FUNCTION__,__LINE__);
	
	return IRQ_HANDLED;
}

static int ntp070_remove(struct i2c_client *client)
{
	struct ntp070_data *ntp070 = i2c_get_clientdata(client);
	
    input_unregister_device(ntp070->input_dev);
    input_free_device(ntp070->input_dev);
    free_irq(client->irq, ntp070);
    kfree(ntp070); 
#ifdef CONFIG_ANDROID_POWER
    android_unregister_early_suspend(&ntp070_early_suspend);
#endif      
    this_client = NULL;
	return 0;
}

#ifdef CONFIG_ANDROID_POWER
static int ntp070_suspend(android_early_suspend_t *h)
{
	struct i2c_client *client = container_of(ntp070_device.parent, struct i2c_client, dev);
	rk28printk("Gsensor mma7760 enter suspend\n");
	return 0;
}

static int ntp070_resume(android_early_suspend_t *h)
{
	struct i2c_client *client = container_of(ntp070_device.parent, struct i2c_client, dev);
    struct ntp070_data *ntp070 = (struct ntp070_data *)i2c_get_clientdata(client);
	rk28printk("Gsensor mma7760 resume!!\n");
	return 0;
}
#else
static int ntp070_suspend(struct i2c_client *client, pm_message_t mesg)
{
	rk28printk("Gsensor mma7760 enter 2 level  suspend\n");
	return 0;
}
static int ntp070_resume(struct i2c_client *client)
{
	struct ntp070_data *ntp070 = (struct ntp070_data *)i2c_get_clientdata(client);
	rk28printk("Gsensor mma7760 2 level resume!!\n");
	return 0;
}
#endif

static const struct i2c_device_id ntp070_id[] = {
		{"ntp070", 0},
		{ }
};

static struct i2c_driver ntp070_driver = {
	.driver = {
		.name = "ntp070",
	    },
	.id_table 	= ntp070_id,
	.probe		= ntp070_probe,
	.remove		= __devexit_p(ntp070_remove),
#ifndef CONFIG_ANDROID_POWER	
	.suspend = &ntp070_suspend,
	.resume = &ntp070_resume,
#endif	
};


static int ntp070_init_client(struct i2c_client *client)
{
	struct ntp070_data *ntp070;
	int ret;
	ntp070 = i2c_get_clientdata(client);
	rk28printk("gpio_to_irq(%d) is %d\n",client->irq,gpio_to_irq(client->irq));
	if ( !gpio_is_valid(client->irq)) {
		rk28printk("+++++++++++gpio_is_invalid\n");
		return -EINVAL;
	}
	ret = gpio_request(client->irq, "ntp070_int");
	if (ret) {
		rk28printk( "failed to request mma7990_trig GPIO%d\n",gpio_to_irq(client->irq));
		return ret;
	}
    ret = gpio_direction_input(client->irq);
    if (ret) {
        rk28printk("failed to set mma7990_trig GPIO gpio input\n");
		return ret;
    }
	gpio_pull_updown(client->irq, GPIOPullUp);
	client->irq = gpio_to_irq(client->irq);
	ret = request_irq(client->irq, ntp070_interrupt, IRQF_TRIGGER_FALLING, client->dev.driver->name, ntp070);
	rk28printk("request irq is %d,ret is  0x%x\n",client->irq,ret);
	if (ret ) {
		rk28printk(KERN_ERR "ntp070_init_client: request irq failed,ret is %d\n",ret);
        return ret;
	}
	enable_irq(client->irq);
 
	return 0;
}

static int  ntp070_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ntp070_data *ntp070;
	int err;
	

	ntp070 = kzalloc(sizeof(struct ntp070_data), GFP_KERNEL);
	if (!ntp070) {
		rk28printk("[ntp070]:alloc data failed.\n");
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}
    
#ifdef TOUCH_TP20466A
		//rk28printk("\n *** tp20466a_i2c_set_regs start *** \n");mdelay(3000);//
		err=tp20466a_i2c_set_regs(client, 0x30, tp20466a_init_data, sizeof(tp20466a_init_data)/sizeof(u8));
		//rk28printk("\n tp20466a_i2c_set_regs=%d\n",err);
		if(err<0) return err;
		//tp20466a_read_test(this_client,0x69,NULL);
#endif
	INIT_WORK(&ntp070->work, ntp070_work_func);

	ntp070->client = client;
	i2c_set_clientdata(client, ntp070);

	this_client = client;

	err = ntp070_init_client(client);
	if (err < 0) {
		rk28printk(KERN_ERR
		       "ntp070_probe: ntp070_init_client failed\n");
		goto exit_request_gpio_irq_failed;
	}
		
	ntp070->input_dev = input_allocate_device();
	if (!ntp070->input_dev) {
		err = -ENOMEM;
		rk28printk(KERN_ERR
		       "ntp070_probe: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}

	input_set_abs_params(ntp070->input_dev, ABS_X,0,LCD_MAX_LENGTH-1,0, 0);
	input_set_abs_params(ntp070->input_dev, ABS_Y,0,LCD_MAX_WIDTH-1,0, 0);
#ifdef TOUCH_TP20466A
	//input_set_abs_params(ntp070->input_dev, ABS_PRESSURE,0,MAX_12BIT,0, 0);
#endif

	ntp070->input_dev->name = "ntp070";
	ntp070->input_dev->dev.parent = &client->dev;
	ntp070->input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	ntp070->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	g_ts_dev = ntp070;

	err = input_register_device(ntp070->input_dev);
	if (err < 0) {
		rk28printk(KERN_ERR
		       "ntp070_probe: Unable to register input device: %s\n",
		       ntp070->input_dev->name);
		goto exit_input_register_device_failed;
	}

#ifdef CONFIG_ANDROID_POWER
    ntp070_early_suspend.suspend = ntp070_suspend;
    ntp070_early_suspend.resume = ntp070_resume;
    ntp070_early_suspend.level = 0x2;
    android_register_early_suspend(&ntp070_early_suspend);
#endif
	rk28printk(KERN_INFO "ntp070 probe ok\n");
	tp_state = TP_STATE_IDLE;
	printk("%s:%d\n",__FUNCTION__,__LINE__);
#if 0	
	ntp070_start(client, MMA7660_RATE_32);
#endif
	return 0;

exit_misc_device_register_ntp070_device_failed:
    input_unregister_device(ntp070->input_dev);
exit_input_register_device_failed:
	input_free_device(ntp070->input_dev);
exit_input_allocate_device_failed:
    free_irq(client->irq, ntp070);
exit_request_gpio_irq_failed:
	kfree(ntp070);	
exit_alloc_data_failed:
    ;
	printk("%s:%d\n",__FUNCTION__,__LINE__);
	return err;
}


static int __init ntp070_i2c_init(void)
{
	printk("%s:%d\n",__FUNCTION__,__LINE__);
	return i2c_add_driver(&ntp070_driver);
}

static void __exit ntp070_i2c_exit(void)
{
	printk("%s:%d\n",__FUNCTION__,__LINE__);
	i2c_del_driver(&ntp070_driver);
}

device_initcall(ntp070_i2c_init);//zfl    module_init        late_initcall_sync
module_exit(ntp070_i2c_exit);



