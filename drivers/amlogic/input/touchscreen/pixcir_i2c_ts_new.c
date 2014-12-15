/*
 * Driver for Pixcir I2C touchscreen controllers.
 *
 * Copyright (C) 2010-2011 Pixcir, Inc.
 *
 * pixcir_i2c_ts.c V3.0	from v3.0 support TangoC solution and remove the previous soltutions
 *
 * pixcir_i2c_ts.c V3.1	Add bootloader function	7
 *			Add RESET_TP		9
 * 			Add ENABLE_IRQ		10
 *			Add DISABLE_IRQ		11
 * 			Add BOOTLOADER_STU	16
 *			Add ATTB_VALUE		17
 *			Add Write/Read Interface for APP software
 *
 * pixcir_i2c_ts.c V3.2.09	for INT_MODE 0x09
 *				change to workqueue for ISR
 *				adaptable report rate self
 *				add power management
 *
 *This code is proprietary and is subject to license terms
 *
 */

#include <linux/delay.h>   
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <asm/uaccess.h>
//#include <linux/smp_lock.h>
#include <mach/gpio.h>
#include <linux/i2c/pixcir_i2c_ts.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static void pixcir_i2c_ts_early_suspend(struct early_suspend *handler);
static void pixcir_i2c_ts_early_resume(struct early_suspend *handler);
#endif


#define test_bit(dat, bitno) ((dat) & (1<<(bitno)))
static int gpio_shutdown = 0;
/*********************************Bee-0928-TOP****************************************/
#define PIXCIR_DEBUG	0	

#define SLAVE_ADDR		0x5c
#define	BOOTLOADER_ADDR		0x5d

#ifndef I2C_MAJOR
#define I2C_MAJOR 		125
#endif

#define I2C_MINORS 		256

#define	CALIBRATION_FLAG	1
#define	BOOTLOADER		7
#define RESET_TP		9

#define	ENABLE_IRQ		10
#define	DISABLE_IRQ		11
#define	BOOTLOADER_STU		16
#define ATTB_VALUE		17

#define	MAX_FINGER_NUM		5

static unsigned char status_reg = 0;
volatile int global_irq;
volatile int work_pending;

struct i2c_dev
{
	struct list_head list;
	struct i2c_adapter *adap;
	struct device *dev;
};

static struct i2c_driver pixcir_i2c_ts_driver;
static struct class *i2c_dev_class;
static LIST_HEAD( i2c_dev_list);
static DEFINE_SPINLOCK( i2c_dev_list_lock);
/* debug switch start */
struct pixcir_i2c_ts_data *tsdata_glob;
static int enable_debug=0;
#define pixcir_dbg(fmt, args...)  { if(enable_debug) printk(fmt, ## args); }
static int pixcir_i2c_read(struct i2c_client *client,uint8_t *cmd,int length1,uint8_t *data, int length);
static ssize_t show_pixcir_debug_flag(struct class* class, struct class_attribute* attr, char* buf)
{
    ssize_t ret = 0;

    ret = sprintf(buf, "%d\n", enable_debug);

    return ret;
}

static ssize_t store_pixcir_debug_flag(struct class* class, struct class_attribute* attr, const char* buf, size_t count)
{
    u32 reg;

    switch(buf[0]) {
        case '1':
            enable_debug=1;
            break;

        case '0':
            enable_debug=0;
            break;

        default:
            printk("unknow command!\n");
    }

    return count;
}

static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	msgs[0].flags=!I2C_M_RD;
	msgs[0].addr=client->addr;
	msgs[0].len=1;
	msgs[0].buf=&buf[0];

	msgs[1].flags=I2C_M_RD;
	msgs[1].addr=client->addr;
	msgs[1].len=len;
	msgs[1].buf=&buf[0];
	
	ret=i2c_transfer(client->adapter,msgs,2);
	return ret;
}


static struct class_attribute pixcir_debug_attrs[]={
  __ATTR(enable_debug, S_IRUGO | S_IWUSR, show_pixcir_debug_flag, store_pixcir_debug_flag),
  __ATTR_NULL
};

static void create_pixcir_debug_attrs(struct class* class)
{
  int i=0;
  for(i=0; pixcir_debug_attrs[i].attr.name; i++){
    class_create_file(class, &pixcir_debug_attrs[i]);
  }
}

static void remove_pixcir_debug_attrs(struct class* class)
{
  int i=0;
  for(i=0; pixcir_debug_attrs[i].attr.name; i++){
    class_remove_file(class, &pixcir_debug_attrs[i]);
  }
}
/* debug switch end */

static void return_i2c_dev(struct i2c_dev *i2c_dev)
{
	spin_lock(&i2c_dev_list_lock);
	list_del(&i2c_dev->list);
	spin_unlock(&i2c_dev_list_lock);
	kfree(i2c_dev);
}

static struct i2c_dev *i2c_dev_get_by_minor(unsigned index)
{
	struct i2c_dev *i2c_dev;
	i2c_dev = NULL;

	spin_lock(&i2c_dev_list_lock);
	list_for_each_entry(i2c_dev, &i2c_dev_list, list)
	{
		if (i2c_dev->adap->nr == index)
			goto found;
	}
	i2c_dev = NULL;
	found: spin_unlock(&i2c_dev_list_lock);
	return i2c_dev;
}

static struct i2c_dev *get_free_i2c_dev(struct i2c_adapter *adap)
{
	struct i2c_dev *i2c_dev;

	if (adap->nr >= I2C_MINORS) {
		printk(KERN_ERR "i2c-dev: Out of device minors (%d)\n",
				adap->nr);
		return ERR_PTR(-ENODEV);
	}

	i2c_dev = kzalloc(sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return ERR_PTR(-ENOMEM);

	i2c_dev->adap = adap;

	spin_lock(&i2c_dev_list_lock);
	list_add_tail(&i2c_dev->list, &i2c_dev_list);
	spin_unlock(&i2c_dev_list_lock);
	return i2c_dev;
}
/*********************************Bee-0928-bottom**************************************/

static struct workqueue_struct *pixcir_wq;

struct pixcir_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work work;
	int irq;
	struct pixcir_i2c_ts_platform_data *pdata;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	u8 button;
	int irq_status;
};


static int attb_read_val(struct pixcir_i2c_ts_data *tsdata)
{
	return gpio_in_get(tsdata->pdata->gpio_irq);
}

static void pixcir_reset(struct pixcir_i2c_ts_data *tsdata)
{

  gpio_out_high(tsdata->pdata->gpio_shutdown);
	msleep(200);
  gpio_out_low(tsdata->pdata->gpio_shutdown);
	msleep(200);

}

static void tangoc_reset(struct pixcir_i2c_ts_data *tsdata)
{ 

  gpio_out_high(tsdata->pdata->gpio_shutdown);
	msleep(100);
  gpio_out_low(tsdata->pdata->gpio_shutdown);
	msleep(20);


}

static void pixcir_init(struct pixcir_i2c_ts_data *tsdata)
{
	mdelay(50);
	gpio_set_status(tsdata->pdata->gpio_irq, gpio_status_in);
	gpio_irq_set(170, GPIO_IRQ( (tsdata->irq-INT_GPIO_0), GPIO_IRQ_FALLING));
}


static int 

pixcir_i2c_transfer(

	struct i2c_client *client, struct i2c_msg *msgs, int cnt)

{

	int ret=0, retry=3;

	while((retry-- )&&(cnt>0)){

		

								//down_interruptible(&i2c.wr_sem);

                ret = i2c_transfer(client->adapter, msgs, cnt);

               // up(&i2c.wr_sem);

                if(ret>0)break;

                	 

	}

	return ret;

}
static int pixcir_i2c_read(struct i2c_client *client,uint8_t *cmd,int length1,uint8_t *data, int length)

{

	int ret;

      struct i2c_msg msgs[] = {

		{.addr = client->addr, .flags = 0, .len = length1, .buf = cmd,},

		{.addr = client->addr, .flags = I2C_M_RD, .len = length, .buf = data,}

        };

    ret = pixcir_i2c_transfer(client, msgs, 2);

	if(ret < 0){

		printk("%s, i2c read error, ret= %d,addr=%2x\n", __func__, ret,msgs[0].addr);

	}

	return ret;

}
struct point_node_t{
	unsigned char 	active ;
	unsigned char	finger_id;
	unsigned int	posx;
	unsigned int	posy;
};

static struct point_node_t point_slot[MAX_FINGER_NUM*2];
static struct point_node_t point_slot_back[10];

int distance[5]={0};
int touch_flage[5]={0};
int touch_back=0;
static u32 key_map[] = {KEY_BACK, KEY_HOMEPAGE, KEY_MENU};
static void pixcir_ts_poscheck(struct work_struct *work)
{
  //     printk("%s\n",__func__);
//	printk("xiehui debug ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~%s\n",__func__);
	struct pixcir_i2c_ts_data *tsdata = container_of(work,
			struct pixcir_i2c_ts_data,
			work.work);
	int x,y;
	unsigned char *p;
	unsigned char touch, button, pix_id,slot_id;
	unsigned char rdbuf[27]={0};// wrbuf[1] = { 0 };
	int slotid[5];
	int ret, i,temp;
	//int flage[5]={1,1,1,1,1};
ret=pixcir_i2c_read(tsdata->client, rdbuf,1,rdbuf,27);
if(ret<0)goto out;
  
	touch = rdbuf[0]&0x07;
	if(touch>5)goto out;
	button = rdbuf[1];
	pixcir_dbg("touch=%d, button=%d\n ",touch,button);
	
	u32 button_changed = tsdata->button ^ button;
	if (button_changed) {
		tsdata->button = button;
		for(i=0; i<3; i++)
			if ((button_changed>>i)&1) {
				input_report_key(tsdata->input, key_map[i], (tsdata->button>>i)&1);
				pixcir_dbg("key(%d) %s\n", key_map[i], ((tsdata->button>>i)&1) ? "down":"up");
			}	
	}

	p=&rdbuf[2];
	for (i=0; i<touch; i++)	{
		pix_id = (*(p+4));
		slot_id = ((pix_id & 7)<<1) | ((pix_id & 8)>>3);
		slotid[i]=slot_id;
		//point_slot[slot_id].active = 1;
		point_slot[slot_id].finger_id = pix_id;	
		point_slot[slot_id].posx = (*(p+1)<<8)+(*(p));
		point_slot[slot_id].posy = (*(p+3)<<8)+(*(p+2));
		
		point_slot[slot_id].active = 1;
		p+=5;
		if(distance[i]==0)
		{
		  point_slot_back[i].posx=point_slot[slot_id].posx;

		  point_slot_back[i].posy=point_slot[slot_id].posy;
			
		}
		//printk("==slotid[%2d]=%2d\n",i,slot_id);
	}

#define dist 10

	if(touch)

		{
		
		for(i=0;i<touch;i++){
		
		x=(point_slot_back[i].posx-point_slot[slotid[i]].posx);

		x=(x>0)?x:-x;

		y=(point_slot_back[i].posy-point_slot[slotid[i]].posy);

		y=(y>0)?y:-y;
		temp=x+y;
		//printk("distance=%2d,%2d,%2d\n",distance[i],temp,touch_flage[i]);
		if(distance[i]){
			if((temp<dist)&&(touch_flage[i]==0))
				{
	
				point_slot[slotid[i]].posx=point_slot_back[i].posx;

		 		point_slot[slotid[i]].posy=point_slot_back[i].posy;
				//printk("report back\n");
				}
			  else 
			    touch_flage[i]=1;
				}
		else 
			distance[i]=1;
			
			}
		
		 }
	else 
		{memset(distance,0,sizeof(distance));
		memset(touch_flage,0,sizeof(touch_flage));		
		}
	
	if(touch) {
		input_report_key(tsdata->input, BTN_TOUCH, 1);
		//input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 15);
		for (i=0; i<MAX_FINGER_NUM*2; i++) {
			if ((point_slot[i].active == 1)
			&&(point_slot[i].posx > tsdata->pdata->xmin)
			&&(point_slot[i].posx < tsdata->pdata->xmax)
			&&(point_slot[i].posy > tsdata->pdata->ymin)
			&&(point_slot[i].posy < tsdata->pdata->ymax)) {
				input_report_key(tsdata->input, ABS_MT_TRACKING_ID, i);
				input_report_abs(tsdata->input, ABS_MT_POSITION_X,  point_slot[i].posx);
				input_report_abs(tsdata->input, ABS_MT_POSITION_Y,  point_slot[i].posy);
				input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 15);
				input_mt_sync(tsdata->input);
				pixcir_dbg("(slot=%1d,x%d=%4d,y%d=%4d)  ",i, i/2,point_slot[i].posx, i/2, point_slot[i].posy);
			}
		}	pixcir_dbg("\n");
	} else {
		input_report_key(tsdata->input, BTN_TOUCH, 0);
		input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);
		pixcir_dbg("finger up\n");
	}
	input_sync(tsdata->input); 

	for (i=0; i<MAX_FINGER_NUM*2; i++) {
		if (point_slot[i].active == 0) {
			point_slot[i].posx = 0;
			point_slot[i].posy = 0;
		}
		point_slot[i].active = 0;
	}
out:	
	enable_irq(tsdata->client->irq);
	tsdata->irq_status = 1;
	//work_pending =                0;
}

static irqreturn_t pixcir_ts_isr(int irq, void *dev_id)
{
	struct pixcir_i2c_ts_data *tsdata = dev_id;
	//printk("%s\n",__func__);
	disable_irq_nosync(tsdata->client->irq);
	tsdata->irq_status = 0;
	//if (!work_pending) {
		//work_pending = 1 ;
		queue_work(pixcir_wq, &tsdata->work.work);
	//}
	return IRQ_HANDLED;
}
#if 0
#ifdef CONFIG_PM_SLEEP
static int pixcir_i2c_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char wrbuf[2] = { 0 };
	int ret;
//printk("%s\n",__func__);
	wrbuf[0] = 0x33;
	wrbuf[1] = 0x03;	//enter into freeze mode;
	/**************************************************************
	wrbuf[1]:	0x00: Active mode
			0x01: Sleep mode
			0xA4: Sleep mode automatically switch
			0x03: Freeze mode
	More details see application note 710 power manangement section
	****************************************************************/
	ret = i2c_master_send(client, wrbuf, 2);
	if(ret!=2) {
		dev_err(&client->dev,
			"%s: i2c_master_send failed(), ret=%d\n",
			__func__, ret);
	}

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int pixcir_i2c_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pixcir_i2c_ts_data *tsdata =	i2c_get_clientdata(client);
///if suspend enter into freeze mode please reset TP
#if 1
//printk("%s\n",__func__);
	pixcir_reset(tsdata);
#else
	unsigned char wrbuf[2] = { 0 };
	int ret;

	wrbuf[0] = 0x33;
	wrbuf[1] = 0;
	ret = i2c_master_send(client, wrbuf, 2);
	if(ret!=2) {
		dev_err(&client->dev,
			"%s: i2c_master_send failed(), ret=%d\n",
			__func__, ret);
	}
#endif
	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pixcir_dev_pm_ops,
			 pixcir_i2c_ts_suspend, pixcir_i2c_ts_resume);
#endif 
static int __devinit pixcir_i2c_ts_probe(struct i2c_client *client,
					 const struct i2c_device_id *id)
{
	struct pixcir_ts_platform_data *pdata = client->dev.platform_data;
	struct pixcir_i2c_ts_data *tsdata;
	struct input_dev *input;
	struct device *dev;
	struct i2c_dev *i2c_dev;
	int i, error;

	if (!pdata) {
		dev_err(&client->dev, "platform data not defined\n");
		return -EINVAL;
	}
	printk("pixcir_i2c_ts_probe!\n");

	for(i=0; i<MAX_FINGER_NUM*2; i++) {
		point_slot[i].active = 0;
	}

	work_pending = 0;

	tsdata = kzalloc(sizeof(*tsdata), GFP_KERNEL);
	tsdata_glob=tsdata;
	input = input_allocate_device();
	if (!tsdata || !input) {
		dev_err(&client->dev, "Failed to allocate driver data!\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	tsdata->client = client;
	tsdata->input = input;
	tsdata->pdata = pdata;
	tsdata->irq = client->irq;
	global_irq = client->irq;

	INIT_WORK(&tsdata->work.work, pixcir_ts_poscheck);

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(EV_ABS, input->evbit);
	__set_bit(EV_SYN, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	__set_bit(ABS_MT_TOUCH_MAJOR, input->absbit);
	__set_bit(ABS_MT_TRACKING_ID, input->absbit);
	__set_bit(ABS_MT_POSITION_X, input->absbit);
	__set_bit(ABS_MT_POSITION_Y, input->absbit);
	__set_bit(KEY_MENU, input->keybit);
	//__set_bit(KEY_HOME, input->keybit);
	__set_bit(KEY_HOMEPAGE,input->keybit);
	__set_bit(KEY_BACK, input->keybit);
	
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_TRACKING_ID, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X, tsdata->pdata->xmin, tsdata->pdata->xmax, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, tsdata->pdata->ymin, tsdata->pdata->ymax, 0, 0);
	error = input_register_device(input);
	if (error) {
		printk("input_register_device Error.\n");
		goto err_free_mem;
	}
	input_set_drvdata(input, tsdata);
	i2c_set_clientdata(client, tsdata);
	/*********************************Bee-0928-TOP****************************************/
	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		printk("get_free_i2c_dev Error.\n");
		error = PTR_ERR(i2c_dev);
		return error;
	}

	dev = device_create(i2c_dev_class, &client->adapter->dev, MKDEV(I2C_MAJOR,
			client->adapter->nr), NULL, "pixcir_i2c_ts%d", client->adapter->nr);

	if (IS_ERR(dev)) {
		printk("device_create Error.\n");
		error = PTR_ERR(dev);
		return error;
	}
	#ifdef CONFIG_HAS_EARLYSUSPEND
	tsdata->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	tsdata->early_suspend.suspend = pixcir_i2c_ts_early_suspend;
	tsdata->early_suspend.resume = pixcir_i2c_ts_early_resume;
	register_early_suspend(&tsdata->early_suspend);
	printk("Register early_suspend done\n");
 #endif
	  
/*********************************Bee-0928-BOTTOM****************************************/
		error = request_irq(client->irq, pixcir_ts_isr,	
			    /*IRQF_TRIGGER_FALLING|*/IRQF_DISABLED,
			    client->name, tsdata);
	if (error) {
		dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
		goto err_free_mem;
	}
	disable_irq_nosync(client->irq);
	tsdata->irq_status = 0;
	pixcir_init(tsdata);
	printk("insmod successfully!\n");
	dev_err(&tsdata->client->dev, "insmod successfully!\n");
	printk("irq=%d\n",client->irq);
	
 pixcir_reset(tsdata);
 	
	unsigned char Rdbuf[10];
	int ret;
 	int  is_pixcir=true;
	memset(Rdbuf, 0, sizeof(Rdbuf));
	Rdbuf[0] = 0;
	ret = i2c_read_bytes(tsdata->client, Rdbuf, 10);
	if (ret != 2){
		printk("is_pixcir  1================== %d\n",is_pixcir);
		is_pixcir = false;
		dev_err(&tsdata->client->dev, "Unable to read i2c page!(%d)\n", ret);
	}


	if (!is_pixcir){
		printk("is_pixcir  2================== %d\n",is_pixcir);

		unregister_early_suspend(&tsdata->early_suspend);

		return_i2c_dev(i2c_dev);
//		device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, client->adapter->nr));
		/*********************************V2.0-Bee-0928-BOTTOM****************************************/

		goto err_free_irq ;

	}


	enable_irq(client->irq);
	tsdata->irq_status = 1;
	return 0;


err_free_irq:

	free_irq(client->irq, tsdata);

err_free_mem:

printk("cancel_delayed_work_sync");
//	cancel_work_sync(&tsdata->work);

	destroy_workqueue(pixcir_wq);
 pixcir_wq=NULL;
//	dev_set_drvdata(&client->dev, NULL);
printk("if (tsdata->input) "); 
 if (tsdata->input)
	{
printk("input_unregister_device\n"); 
		input_unregister_device(tsdata->input);
printk("input_free_device\n"); 
		input_free_device(tsdata->input);
	}
	kfree(tsdata);
	
	return error;
}

static int __devexit pixcir_i2c_ts_remove(struct i2c_client *client)
{
	int error;
	struct i2c_dev *i2c_dev;
	struct pixcir_i2c_ts_data *tsdata = i2c_get_clientdata(client);

	//device_init_wakeup(&client->dev, 0);

	mb();
	free_irq(client->irq, tsdata);

	/*********************************Bee-0928-TOP****************************************/
	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		return error;
	}

	return_i2c_dev(i2c_dev);
	device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, client->adapter->nr));
	/*********************************Bee-0928-BOTTOM****************************************/

	input_unregister_device(tsdata->input);
	kfree(tsdata);

	dev_set_drvdata(&client->dev, NULL);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&tsdata->early_suspend);
#endif

	return 0;
}

static int pixcir_i2c_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	unsigned char buf[2]={0x33, 0x03};
	
	struct pixcir_i2c_ts_data *tsdata = dev_get_drvdata(&client->dev);
//	cancel_delayed_work_sync(&tsdata->work);
	int ret = 0;
	ret = cancel_work_sync(&tsdata->work);
	if(ret)
	{
		printk("pixcir suspend cancel work sync ret = %d \n",ret);
		int count = 0;
		while(count<20&&ret)
		{
			mdelay(5);
			ret = cancel_work_sync(&tsdata->work);
			count++;
		}
		printk("pixcir cancel work sync again %d times \n",count);
	}
	if (tsdata->irq_status){
		disable_irq_nosync(tsdata->irq);
		tsdata->irq_status = 0;
	}
	i2c_master_send(client, buf, 2);
	return 0;
}

static int pixcir_i2c_ts_resume(struct i2c_client *client)
{

//	printk("xiehui  debug ~~~~~~~~~~~~pixcir_i2c_ts_resume\n");
	struct pixcir_i2c_ts_data *tsdata = dev_get_drvdata(&client->dev);
	pixcir_reset(tsdata);
	if (!tsdata->irq_status){
		enable_irq(tsdata->irq);
		tsdata->irq_status = 1;
	}

	return 0;
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static void pixcir_i2c_ts_early_suspend(struct early_suspend *handler)
{
	struct pixcir_i2c_ts_data *tsdata = container_of(handler,	struct pixcir_i2c_ts_data, early_suspend);
	printk("%s\n", __FUNCTION__);
	 pixcir_i2c_ts_suspend(tsdata->client, PMSG_SUSPEND);
}

static void pixcir_i2c_ts_early_resume(struct early_suspend *handler)
{
	struct pixcir_i2c_ts_data *tsdata = container_of(handler,	struct pixcir_i2c_ts_data, early_suspend);
	printk("%s\n", __FUNCTION__);
	pixcir_i2c_ts_resume(tsdata->client);
}
#endif */ // #ifdef CONFIG_HAS_EARLYSUSPEND
/*************************************Bee-0928****************************************/
/*                        	     pixcir_open                                     */
/*************************************Bee-0928****************************************/
static int pixcir_open(struct inode *inode, struct file *file)
{
	int subminor;
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	struct i2c_dev *i2c_dev;
	int ret = 0;
#if PIXCIR_DEBUG
	printk("enter pixcir_open function\n");
#endif
	subminor = iminor(inode);
/*#if PIXCIR_DEBUG
	printk("subminor=%d\n",subminor);
#endif*/

	//lock_kernel();
	i2c_dev = i2c_dev_get_by_minor(subminor);
	if (!i2c_dev) {
		printk("error i2c_dev\n");
		return -ENODEV;
	}

	adapter = i2c_get_adapter(i2c_dev->adap->nr);
	if (!adapter) {
		return -ENODEV;
	}
	
	client = kzalloc(sizeof(*client), GFP_KERNEL);

	if (!client) {
		i2c_put_adapter(adapter);
		ret = -ENOMEM;
	}

	snprintf(client->name, I2C_NAME_SIZE, "pixcir_i2c_ts%d", adapter->nr);
	client->driver = &pixcir_i2c_ts_driver;
	client->adapter = adapter;
	
	file->private_data = client;
	return 0;
}

/*************************************Bee-0928****************************************/
/*                        	     pixcir_ioctl                                    */
/*************************************Bee-0928****************************************/
static long pixcir_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = tsdata_glob->client;//(struct i2c_client *) file->private_data;
	struct pixcir_i2c_ts_data *tsdata =tsdata_glob;//	i2c_get_clientdata(client);
#if PIXCIR_DEBUG
	printk("pixcir_ioctl(),cmd = %d,arg = %ld\n", cmd, arg);
#endif

	switch (cmd)
	{
	case CALIBRATION_FLAG:	//CALIBRATION_FLAG = 1
	
       #if PIXCIR_DEBUG
		printk("CALIBRATION\n");
#endif

		client->addr = SLAVE_ADDR;
		status_reg = CALIBRATION_FLAG;
		break;

	case BOOTLOADER:	//BOOTLOADER = 7
		client->addr = BOOTLOADER_ADDR;
		status_reg = BOOTLOADER;

		tangoc_reset(tsdata);
		//mdelay(5);
		break;

	case RESET_TP:		//RESET_TP = 9
		pixcir_reset(tsdata);
		break;
		
	case ENABLE_IRQ:	//ENABLE_IRQ = 10
		status_reg = 0;
		enable_irq(global_irq);
		break;
		
	case DISABLE_IRQ:	//DISABLE_IRQ = 11
		disable_irq_nosync(global_irq);
		break;

	case BOOTLOADER_STU:	//BOOTLOADER_STU = 16
		client->addr = BOOTLOADER_ADDR;
		status_reg = BOOTLOADER_STU;

		tangoc_reset(tsdata);
		//mdelay(5);

	case ATTB_VALUE:	//ATTB_VALUE = 17
		client->addr = SLAVE_ADDR;
		status_reg = ATTB_VALUE;
		break;

	default:
		client->addr = SLAVE_ADDR;
		status_reg = 0;
		break;
	}
	return 0;
}

/***********************************Bee-0928****************************************/
/*                        	  pixcir_read                                      */
/***********************************Bee-0928****************************************/
static ssize_t pixcir_read (struct file *file, char __user *buf, size_t count,loff_t *offset)
{
	struct i2c_client *client = tsdata_glob->client;;//(struct i2c_client *)file->private_data;
	struct pixcir_i2c_ts_data *tsdata =	tsdata_glob;//i2c_get_clientdata(client);

	unsigned char *tmp, bootloader_stu[4], attb_value[1];
	int ret = 0;

	switch(status_reg)
	{
	case BOOTLOADER_STU:
		ret=i2c_master_recv(client, bootloader_stu, sizeof(bootloader_stu));
		if (ret!=sizeof(bootloader_stu)) {
			dev_err(&client->dev,
				"%s: BOOTLOADER_STU: i2c_master_recv() failed, ret=%d\n",
				__func__, ret);
			return -EFAULT;
		}

		ret = copy_to_user(buf, bootloader_stu, sizeof(bootloader_stu));
		if(ret)	{
			dev_err(&client->dev,
				"%s: BOOTLOADER_STU: copy_to_user() failed.\n",	__func__);
			return -EFAULT;
		}else {
			ret = 4;
		}
		break;

	case ATTB_VALUE:
		attb_value[0] = attb_read_val(tsdata);
		if(copy_to_user(buf, attb_value, sizeof(attb_value))) {
			dev_err(&client->dev,
				"%s: ATTB_VALUE: copy_to_user() failed.\n", __func__);
			return -EFAULT;
		}else {
			ret = 1;
		}
		break;

	default:
		tmp = kmalloc(count,GFP_KERNEL);
		if (tmp==NULL)
			return -ENOMEM;

		ret = i2c_master_recv(client, tmp, count);
		if (ret != count) {
			dev_err(&client->dev,
				"%s: default: i2c_master_recv() failed, ret=%d\n",
				__func__, ret);
			return -EFAULT;
		}

		if(copy_to_user(buf, tmp, count)) {
			dev_err(&client->dev,
				"%s: default: copy_to_user() failed.\n", __func__);
			kfree(tmp);
			return -EFAULT;
		}
		kfree(tmp);
		break;
	}
	return ret;
}

/***********************************Bee-0928****************************************/
/*                        	  pixcir_write                                     */
/***********************************Bee-0928****************************************/
static ssize_t pixcir_write(struct file *file,const char __user *buf,size_t count, loff_t *ppos)
{
	struct i2c_client *client;
	unsigned char *tmp, bootload_data[143];
	int ret=0, i=0;

	client = tsdata_glob->client;;//file->private_data;

	switch(status_reg)
	{
	case CALIBRATION_FLAG:	//CALIBRATION_FLAG=1
		tmp = kmalloc(count,GFP_KERNEL);
		if (tmp==NULL)
			return -ENOMEM;

		if (copy_from_user(tmp,buf,count)) { 	
			dev_err(&client->dev,
				"%s: CALIBRATION_FLAG: copy_from_user() failed.\n", __func__);
			ret= -EFAULT;
		}
		
		tmp[0]=0x3a;
		int retry=3;
		while(retry--){
  // 	printk("pixcir_i2c  calibration %2x,%2x,%2x \n",tmp[0],tmp[1],count);
			ret = i2c_master_send(client,tmp, count);
			if (ret==count )break; 
				pixcir_reset(tsdata_glob);
		}
		if (ret!=count ) {
			dev_err(&client->dev,
				"%s: CALIBRATION: i2c_master_send() failed, ret=%d\n",
				__func__, ret);
		
			ret= -EFAULT;
			
		}

    msleep(2000);		
		enable_irq(client->irq);
		kfree(tmp);
		break;

	case BOOTLOADER:
		memset(bootload_data, 0, sizeof(bootload_data));

		if (copy_from_user(bootload_data, buf, count)) {
			dev_err(&client->dev,
				"%s: BOOTLOADER: copy_from_user() failed.\n", __func__);
			return -EFAULT;
		}

		ret = i2c_master_send(client, bootload_data, count);
		if(ret!=count) {
			dev_err(&client->dev,
				"%s: BOOTLOADER: i2c_master_send() failed, ret = %d\n",
				__func__, ret);
			return -EFAULT;
		}
		break;

	default:
		tmp = kmalloc(count,GFP_KERNEL);
		if (tmp==NULL)
			return -ENOMEM;

		if (copy_from_user(tmp,buf,count)) { 	
			dev_err(&client->dev,
				"%s: default: copy_from_user() failed.\n", __func__);
			kfree(tmp);
			return -EFAULT;
		}
		
		ret = i2c_master_send(client,tmp,count);
		if (ret!=count ) {
			dev_err(&client->dev,
				"%s: default: i2c_master_send() failed, ret=%d\n",
				__func__, ret);
			kfree(tmp);
			return -EFAULT;
		}
		kfree(tmp);
		break;
	}
	return ret;
}

/***********************************Bee-0928****************************************/
/*                        	  pixcir_release                                   */
/***********************************Bee-0928****************************************/
static int pixcir_release(struct inode *inode, struct file *file)
{
	struct i2c_client *client = file->private_data;
#if PIXCIR_DEBUG
	printk("enter pixcir_release funtion\n");
#endif
	i2c_put_adapter(client->adapter);
	kfree(client);
	file->private_data = NULL;

	return 0;
}

/*********************************Bee-0928-TOP****************************************/
static const struct file_operations pixcir_i2c_ts_fops =
{	.owner		= THIS_MODULE,
	.read		= pixcir_read,
	.write		= pixcir_write,
	.open		= pixcir_open,
	.unlocked_ioctl = pixcir_ioctl,
	.release	= pixcir_release,
};
/*********************************Bee-0928-BOTTOM****************************************/


static const struct i2c_device_id pixcir_i2c_ts_id[] = {
	{ "pixcir168", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pixcir_i2c_ts_id);

static struct i2c_driver pixcir_i2c_ts_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "pixcir_i2c_ts_v3.2.09",
	//	.pm	= &pixcir_dev_pm_ops,
	},
	.probe		= pixcir_i2c_ts_probe,
	.remove		= __devexit_p(pixcir_i2c_ts_remove),
	.id_table	= pixcir_i2c_ts_id,
};

static int __init pixcir_i2c_ts_init(void)
{
	int ret;
	pixcir_wq = create_singlethread_workqueue("pixcir_wq");
	if(!pixcir_wq)
		return -ENOMEM;


	/*********************************Bee-0928-TOP****************************************/
	ret = register_chrdev(I2C_MAJOR,"pixcir_i2c_ts",&pixcir_i2c_ts_fops);
	if (ret) {
		printk(KERN_ERR "%s:register chrdev failed\n",__FILE__);
		return ret;
	}

	i2c_dev_class = class_create(THIS_MODULE, "pixcir_i2c_dev");
	if (IS_ERR(i2c_dev_class)) {
		ret = PTR_ERR(i2c_dev_class);
		class_destroy(i2c_dev_class);
	}
	/********************************Bee-0928-BOTTOM******************************************/
	create_pixcir_debug_attrs(i2c_dev_class);
	return i2c_add_driver(&pixcir_i2c_ts_driver);
}
#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(pixcir_i2c_ts_init);
#else
module_init(pixcir_i2c_ts_init);
#endif

static void __exit pixcir_i2c_ts_exit(void)
{
	i2c_del_driver(&pixcir_i2c_ts_driver);
	/********************************Bee-0928-TOP******************************************/
	remove_pixcir_debug_attrs(i2c_dev_class);
	class_destroy(i2c_dev_class);
	unregister_chrdev(I2C_MAJOR,"pixcir_i2c_ts");
	/********************************Bee-0928-BOTTOM******************************************/
	if(pixcir_wq)
		destroy_workqueue(pixcir_wq);
}
module_exit(pixcir_i2c_ts_exit);

MODULE_AUTHOR("Jianchun Bian <jcbian@pixcir.com.cn>");
MODULE_DESCRIPTION("Pixcir I2C Touchscreen Driver");
MODULE_LICENSE("Pixcir Proprietary");

