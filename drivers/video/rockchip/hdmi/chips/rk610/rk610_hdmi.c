#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/i2c.h>
#include "rk610_hdmi.h"

struct rk610_hdmi_pdata *rk610_hdmi = NULL;
struct hdmi *hdmi=NULL;

extern struct rk_lcdc_device_driver * rk_get_lcdc_drv(char *name);
extern void hdmi_register_display_sysfs(struct hdmi *hdmi, struct device *parent);
extern void hdmi_unregister_display_sysfs(struct hdmi *hdmi);

int rk610_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
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
	queue_delayed_work(rk610_hdmi->workqueue, &rk610_hdmi->delay_work, 100);
	#endif
	queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));	
	mutex_unlock(&hdmi->enable_mutex);
	return;
}
#endif

static void rk610_irq_work_func(struct work_struct *work)
{
	if(hdmi->suspend == 0) {
		if(hdmi->enable == 1) {
			rk610_hdmi_interrupt();
			if(hdmi->hdcp_irq_cb)
				hdmi->hdcp_irq_cb(0);
		}
		#ifndef HDMI_USE_IRQ
		queue_delayed_work(rk610_hdmi->workqueue, &rk610_hdmi->delay_work, 50);
		#endif
	}
}

#ifdef HDMI_USE_IRQ
static irqreturn_t rk610_irq(int irq, void *dev_id)
{
	printk(KERN_INFO "rk610 irq triggered.\n");
	schedule_work(&rk610_hdmi->irq_work);
    return IRQ_HANDLED;
}
#endif

static int rk610_hdmi_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int rc = 0;
	
	rk610_hdmi = kzalloc(sizeof(struct rk610_hdmi_pdata), GFP_KERNEL);
	if(!rk610_hdmi)
	{
        dev_err(&client->dev, "no memory for state\n");
    	return -ENOMEM;
    }
	rk610_hdmi->client = client;
	i2c_set_clientdata(client, rk610_hdmi);
	
	hdmi = kmalloc(sizeof(struct hdmi), GFP_KERNEL);
	if(!hdmi)
	{
    	dev_err(&client->dev, "rk610 hdmi kmalloc fail!");
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
	hdmi->xscale = 95;
	hdmi->yscale = 95;
	hdmi->insert = rk610_hdmi_sys_insert;
	hdmi->remove = rk610_hdmi_sys_remove;
	hdmi->control_output = rk610_hdmi_sys_enalbe_output;
	hdmi->config_video = rk610_hdmi_sys_config_video;
	hdmi->config_audio = rk610_hdmi_sys_config_audio;
	hdmi->detect_hotplug = rk610_hdmi_sys_detect_hpd;
	hdmi->read_edid = rk610_hdmi_sys_read_edid;
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
	
	rk610_hdmi_sys_init();
	
#ifdef HDMI_USE_IRQ
	if(client->irq != INVALID_GPIO) {
		INIT_WORK(&rk610_hdmi->irq_work, rk610_irq_work_func);
		if((rc = gpio_request(client->irq, "hdmi gpio")) < 0)
	    {
	        dev_err(&client->dev, "fail to request gpio %d\n", client->irq);
	        goto err_request_lcdc;
	    }
	    hdmi->irq = gpio_to_irq(client->irq);
		rk610_hdmi->gpio = client->irq;
	    gpio_pull_updown(client->irq, GPIOPullUp);
	    gpio_direction_input(client->irq);
	    if((rc = request_irq(rk610_hdmi->irq, rk610_irq, IRQF_TRIGGER_RISING, NULL, hdmi)) < 0)
	    {
	        dev_err(&client->dev, "fail to request hdmi irq\n");
	        goto err_request_irq;
	    }
	}
	else
#else
	{
		rk610_hdmi->workqueue = create_singlethread_workqueue("rk610 irq");
		INIT_DELAYED_WORK(&(rk610_hdmi->delay_work), rk610_irq_work_func);
		rk610_irq_work_func(NULL);
	}
#endif

	dev_info(&client->dev, "rk610 hdmi i2c probe ok\n");
	
    return 0;
	
err_request_irq:
	gpio_free(client->irq);
err_request_lcdc:
	kfree(hdmi);
	hdmi = NULL;
err_kzalloc_hdmi:
	kfree(rk610_hdmi);
	rk610_hdmi = NULL;
	dev_err(&client->dev, "rk610 hdmi probe error\n");
	return rc;

}

static int __devexit rk610_hdmi_i2c_remove(struct i2c_client *client)
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

static void rk610_hdmi_i2c_shutdown(struct i2c_client *client)
{
	if(hdmi) {
		#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi->early_suspend);
		#endif
	}
	printk(KERN_INFO "rk610 hdmi shut down.\n");
}

static const struct i2c_device_id rk610_hdmi_id[] = {
	{ "rk610_hdmi", 0 },
	{ }
};

static struct i2c_driver rk610_hdmi_i2c_driver = {
    .driver = {
        .name  = "rk610_hdmi",
        .owner = THIS_MODULE,
    },
    .probe      = rk610_hdmi_i2c_probe,
    .remove     = rk610_hdmi_i2c_remove,
    .shutdown	= rk610_hdmi_i2c_shutdown,
    .id_table	= rk610_hdmi_id,
};

static int __init rk610_hdmi_init(void)
{
    return i2c_add_driver(&rk610_hdmi_i2c_driver);
}

static void __exit rk610_hdmi_exit(void)
{
    i2c_del_driver(&rk610_hdmi_i2c_driver);
}

module_init(rk610_hdmi_init);
//fs_initcall(rk610_init);
module_exit(rk610_hdmi_exit);
