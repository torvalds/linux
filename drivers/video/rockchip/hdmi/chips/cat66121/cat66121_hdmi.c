#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/i2c.h>
#include "cat66121_hdmi.h"
#include "cat66121_hdmi_hw.h"

struct cat66121_hdmi_pdata *cat66121_hdmi = NULL;
struct hdmi *hdmi=NULL;

extern struct rk_lcdc_device_driver * rk_get_lcdc_drv(char *name);
extern void hdmi_register_display_sysfs(struct hdmi *hdmi, struct device *parent);
extern void hdmi_unregister_display_sysfs(struct hdmi *hdmi);
static void check_status_func(struct work_struct *work);
static void cat66121_irq_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(check_status_work,check_status_func);

static void check_status_func(struct work_struct *work)
{
	if(HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS) & B_TX_INT_ACTIVE){
		cat66121_irq_work_func(NULL);
	}
	schedule_delayed_work(&check_status_work, msecs_to_jiffies(5000));
}
#if 0
int cat66121_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
					 void (*hdcp_irq_cb)(int status),
					 int (*hdcp_power_on_cb)(void),
					 void (*hdcp_power_off_cb)(void))
{
	hdmi->hdcp_cb = hdcp_cb;
	hdmi->hdcp_irq_cb = hdcp_irq_cb;
	hdmi->hdcp_power_on_cb = hdcp_power_on_cb;
	hdmi->hdcp_power_off_cb = hdcp_power_off_cb;
	
	return HDMI_ERROR_SUCESS;
}
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
static void hdmi_early_suspend(struct early_suspend *h)
{
	hdmi_dbg(hdmi->dev, "hdmi enter early suspend pwr %d state %d\n", hdmi->pwr_mode, hdmi->state);
	flush_delayed_work(&hdmi->delay_work);	
	mutex_lock(&hdmi->enable_mutex);
	hdmi->suspend = 1;
	if(!hdmi->enable) {
		mutex_unlock(&hdmi->enable_mutex);
		return;
	}
	
	#ifdef HDMI_USE_IRQ
	if(hdmi->irq)
		disable_irq(hdmi->irq);
	#endif
	
	mutex_unlock(&hdmi->enable_mutex);
	hdmi->command = HDMI_CONFIG_ENABLE;
	init_completion(&hdmi->complete);
	hdmi->wait = 1;
	queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, 0);
	wait_for_completion_interruptible_timeout(&hdmi->complete,
							msecs_to_jiffies(5000));
	flush_delayed_work(&hdmi->delay_work);
	cancel_delayed_work_sync(&check_status_work);
	return;
}

static void hdmi_early_resume(struct early_suspend *h)
{
	hdmi_dbg(hdmi->dev, "hdmi exit early resume\n");
	mutex_lock(&hdmi->enable_mutex);
	
	hdmi->suspend = 0;
	#ifdef HDMI_USE_IRQ
	if(hdmi->enable && hdmi->irq) {
		enable_irq(hdmi->irq);
	}
	#else
	queue_delayed_work(cat66121_hdmi->workqueue, &cat66121_hdmi->delay_work, 100);
	#endif
	queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));	
	schedule_delayed_work(&check_status_work, msecs_to_jiffies(5000));
	mutex_unlock(&hdmi->enable_mutex);
	return;
}
#endif

static void cat66121_irq_work_func(struct work_struct *work)
{
	if(hdmi->suspend == 0) {
		if(hdmi->enable == 1) {
			cat66121_hdmi_interrupt();
			if(hdmi->hdcp_irq_cb)
				hdmi->hdcp_irq_cb(0);
		}
		#ifndef HDMI_USE_IRQ
		queue_delayed_work(cat66121_hdmi->workqueue, &cat66121_hdmi->delay_work, 100);
		#endif
	}
}

#ifdef HDMI_USE_IRQ
static irqreturn_t cat66121_irq(int irq, void *dev_id)
{
	printk(KERN_INFO "cat66121 irq triggered.\n");
	schedule_work(&cat66121_hdmi->irq_work);
    return IRQ_HANDLED;
}
#endif
#ifdef HDMI_DEBUG
static int hdmi_read_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_recv(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}

static int hdmi_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_send(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}
static ssize_t hdmi_show_reg_attrs(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{

	int i,size=0;
	char val;
	struct i2c_client *client=cat66121_hdmi->client;

	for(i=0;i<256;i++)
	{
		hdmi_read_p0_reg(client, i,  &val);
		if(i%16==0)
			size += sprintf(buf+size,"\n>>>hdmi_hdmi %x:",i);
		size += sprintf(buf+size," %2x",val);
	}

	return size;
}
static ssize_t hdmi_store_reg_attrs(struct device *dev,
						struct device_attribute *attr,
			 			const char *buf, size_t size)
{
	struct i2c_client *client=NULL;
	static char val=0,reg=0;
	client = cat66121_hdmi->client;
	printk("/**********hdmi reg config******/");

	sscanf(buf, "%x%x", &val,&reg);
	hdmi_write_p0_reg(client, reg,  &val);
	return size;
}

static struct device_attribute hdmi_attrs[] = {
	__ATTR(reg_ctl, 0777,hdmi_show_reg_attrs,hdmi_store_reg_attrs),
};
#endif
static int cat66121_hdmi_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int rc = 0;
	struct rk_hdmi_platform_data *pdata = client->dev.platform_data;
	
	cat66121_hdmi = kzalloc(sizeof(struct cat66121_hdmi_pdata), GFP_KERNEL);
	if(!cat66121_hdmi)
	{
        dev_err(&client->dev, "no memory for state\n");
    	return -ENOMEM;
    }
	cat66121_hdmi->client = client;
	i2c_set_clientdata(client, cat66121_hdmi);
	
	hdmi = kmalloc(sizeof(struct hdmi), GFP_KERNEL);
	if(!hdmi)
	{
    	dev_err(&client->dev, "cat66121 hdmi kmalloc fail!");
    	goto err_kzalloc_hdmi;
	}
	memset(hdmi, 0, sizeof(struct hdmi));
	hdmi->dev = &client->dev;
	
	if(HDMI_SOURCE_DEFAULT == HDMI_SOURCE_LCDC0)
		hdmi->lcdc = rk_get_lcdc_drv("lcdc0");
	else
		hdmi->lcdc = rk_get_lcdc_drv("lcdc1");
	if(hdmi->lcdc == NULL)
	{
		dev_err(hdmi->dev, "can not connect to video source lcdc\n");
		rc = -ENXIO;
		goto err_request_lcdc;
	}
	if(pdata->io_init){
		if(pdata->io_init()<0){
			dev_err(&client->dev, "fail to rst chip\n");
			goto err_request_lcdc;
		}
	}
	if(cat66121_detect_device()!=1){
		dev_err(hdmi->dev, "can't find it6610 device \n");
		rc = -ENXIO;
		goto err_request_lcdc;
	}

	hdmi->xscale = 100;
	hdmi->yscale = 100;
	hdmi->insert = cat66121_hdmi_sys_insert;
	hdmi->remove = cat66121_hdmi_sys_remove;
	hdmi->control_output = cat66121_hdmi_sys_enalbe_output;
	hdmi->config_video = cat66121_hdmi_sys_config_video;
	hdmi->config_audio = cat66121_hdmi_sys_config_audio;
	hdmi->detect_hotplug = cat66121_hdmi_sys_detect_hpd;
	hdmi->read_edid = cat66121_hdmi_sys_read_edid;
	hdmi_sys_init();
	
	hdmi->workqueue = create_singlethread_workqueue("hdmi");
	INIT_DELAYED_WORK(&(hdmi->delay_work), hdmi_work);
	
	#ifdef CONFIG_HAS_EARLYSUSPEND
	hdmi->early_suspend.suspend = hdmi_early_suspend;
	hdmi->early_suspend.resume = hdmi_early_resume;
	hdmi->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 10;
	register_early_suspend(&hdmi->early_suspend);
	#endif
	
	hdmi_register_display_sysfs(hdmi, NULL);
	#ifdef CONFIG_SWITCH
	hdmi->switch_hdmi.name="hdmi";
	switch_dev_register(&(hdmi->switch_hdmi));
	#endif
		
	spin_lock_init(&hdmi->irq_lock);
	mutex_init(&hdmi->enable_mutex);
	

	cat66121_hdmi_sys_init();
#ifdef HDMI_DEBUG
	device_create_file(&(client->dev), &hdmi_attrs[0]);
#endif

#ifdef HDMI_USE_IRQ
	if(client->irq != INVALID_GPIO) {
		INIT_WORK(&cat66121_hdmi->irq_work, cat66121_irq_work_func);
		schedule_work(&cat66121_hdmi->irq_work);
		if((rc = gpio_request(client->irq, "hdmi gpio")) < 0)
	    {
	        dev_err(&client->dev, "fail to request gpio %d\n", client->irq);
	        goto err_request_lcdc;
	    }

		schedule_delayed_work(&check_status_work, msecs_to_jiffies(5000));
	    hdmi->irq = gpio_to_irq(client->irq);
		cat66121_hdmi->gpio = client->irq;
	    gpio_pull_updown(client->irq, GPIOPullUp);
	    gpio_direction_input(client->irq);
	    if((rc = request_irq(hdmi->irq, cat66121_irq, IRQF_TRIGGER_FALLING, NULL, hdmi)) < 0)
	    {
	        dev_err(&client->dev, "fail to request hdmi irq\n");
	        goto err_request_irq;
	    }
	}
	else
#else
	{
		cat66121_hdmi->workqueue = create_singlethread_workqueue("cat66121 irq");
		INIT_DELAYED_WORK(&(cat66121_hdmi->delay_work), cat66121_irq_work_func);
		cat66121_irq_work_func(NULL);
	}
#endif

	dev_info(&client->dev, "cat66121 hdmi i2c probe ok\n");
	
    return 0;
	
err_request_irq:
	gpio_free(client->irq);
err_request_lcdc:
	kfree(hdmi);
	hdmi = NULL;
err_kzalloc_hdmi:
	kfree(cat66121_hdmi);
	cat66121_hdmi = NULL;
	dev_err(&client->dev, "cat66121 hdmi probe error\n");
	return rc;

}

static int __devexit cat66121_hdmi_i2c_remove(struct i2c_client *client)
{	
	hdmi_dbg(hdmi->dev, "%s\n", __func__);
	if(hdmi) {
		mutex_lock(&hdmi->enable_mutex);
		if(!hdmi->suspend && hdmi->enable && hdmi->irq)
			disable_irq(hdmi->irq);
		mutex_unlock(&hdmi->enable_mutex);
		if(hdmi->irq)
			free_irq(hdmi->irq, NULL);
		flush_workqueue(hdmi->workqueue);
		destroy_workqueue(hdmi->workqueue);
		#ifdef CONFIG_SWITCH
		switch_dev_unregister(&(hdmi->switch_hdmi));
		#endif
		hdmi_unregister_display_sysfs(hdmi);
		#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi->early_suspend);
		#endif
		fb_destroy_modelist(&hdmi->edid.modelist);
		if(hdmi->edid.audio)
			kfree(hdmi->edid.audio);
		if(hdmi->edid.specs)
		{
			if(hdmi->edid.specs->modedb)
				kfree(hdmi->edid.specs->modedb);
			kfree(hdmi->edid.specs);
		}
		kfree(hdmi);
		hdmi = NULL;
	}
    return 0;
}

static void cat66121_hdmi_i2c_shutdown(struct i2c_client *client)
{
	if(hdmi) {
		#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi->early_suspend);
		#endif
	}
	printk(KERN_INFO "cat66121 hdmi shut down.\n");
}

static const struct i2c_device_id cat66121_hdmi_id[] = {
	{ "cat66121_hdmi", 0 },
	{ }
};

static struct i2c_driver cat66121_hdmi_i2c_driver = {
    .driver = {
        .name  = "cat66121_hdmi",
        .owner = THIS_MODULE,
    },
    .probe      = cat66121_hdmi_i2c_probe,
    .remove     = cat66121_hdmi_i2c_remove,
    .shutdown	= cat66121_hdmi_i2c_shutdown,
    .id_table	= cat66121_hdmi_id,
};

static int __init cat66121_hdmi_init(void)
{
    return i2c_add_driver(&cat66121_hdmi_i2c_driver);
}

static void __exit cat66121_hdmi_exit(void)
{
    i2c_del_driver(&cat66121_hdmi_i2c_driver);
}

//module_init(cat66121_hdmi_init);
device_initcall_sync(cat66121_hdmi_init);
module_exit(cat66121_hdmi_exit);
