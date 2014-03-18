#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif
#include <linux/of_gpio.h>
#include <linux/of_device.h>

#include "cat66121_hdmi.h"
#include "cat66121_hdmi_hw.h"

#define HDMI_POLL_MDELAY 	50//100
struct cat66121_hdmi_pdata *cat66121_hdmi = NULL;
struct hdmi *hdmi=NULL;

extern struct rk_lcdc_driver * rk_get_lcdc_drv(char *name);
extern void hdmi_register_display_sysfs(struct hdmi *hdmi, struct device *parent);
extern void hdmi_unregister_display_sysfs(struct hdmi *hdmi);
static void cat66121_irq_work_func(struct work_struct *work);
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
	
	if(hdmi->irq != INVALID_GPIO)
		disable_irq(hdmi->irq);
	
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
	if(hdmi->irq == INVALID_GPIO){
		queue_delayed_work(cat66121_hdmi->workqueue, &cat66121_hdmi->delay_work, HDMI_POLL_MDELAY);
	}else if(hdmi->enable){
		enable_irq(hdmi->irq);
	}
	queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));	
	mutex_unlock(&hdmi->enable_mutex);
	return;
}
#endif

static void cat66121_irq_work_func(struct work_struct *work)
{
	if(hdmi->suspend == 0) {
		if(hdmi->enable == 1) {
			cat66121_hdmi_interrupt(hdmi);
			if(hdmi->hdcp_irq_cb)
				hdmi->hdcp_irq_cb(0);
		}
		if(!gpio_is_valid(hdmi->irq)){
			queue_delayed_work(cat66121_hdmi->workqueue, &cat66121_hdmi->delay_work, HDMI_POLL_MDELAY);
		}
	}
}

static irqreturn_t cat66121_thread_interrupt(int irq, void *dev_id)
{
	cat66121_irq_work_func(NULL);
	msleep(HDMI_POLL_MDELAY);
	hdmi_dbg(hdmi->dev, "%s irq=%d\n", __func__,irq);
	return IRQ_HANDLED;
}

#if defined(CONFIG_DEBUG_FS)
static int hdmi_read_p0_reg(struct i2c_client *client, char reg, char *val)
{
	//return i2c_master_reg8_recv(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;	//TODO Daisen
	return 0;
}

static int hdmi_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	//return i2c_master_reg8_send(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;	//TODO Daisen
	return 0;
}
static int hdmi_reg_show(struct seq_file *s, void *v)
{

	int i;
	char val;
	struct i2c_client *client=cat66121_hdmi->client;

	for(i=0;i<256;i++)
	{
		hdmi_read_p0_reg(client, i,  &val);
		if(i%16==0)
			seq_printf(s,"\n>>>hdmi_hdmi %x:",i);
		seq_printf(s," %2x",val);
	}
	seq_printf(s,"\n");

	return 0;
}

static ssize_t hdmi_reg_write (struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{ 
	struct i2c_client *client=NULL;
	u32 reg,val;
	char kbuf[25];
	client = cat66121_hdmi->client;
	
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	sscanf(kbuf, "%x%x", &reg,&val);
	hdmi_write_p0_reg(client, reg,  (u8*)&val);

	return count;
}

static int hdmi_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file,hdmi_reg_show,hdmi);
}

static const struct file_operations hdmi_reg_fops = {
	.owner		= THIS_MODULE,
	.open		= hdmi_reg_open,
	.read		= seq_read,
	.write          = hdmi_reg_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int rk_hdmi_drv_init(struct hdmi *hdmi_drv)
{
	int ret = 0;
	struct rk_screen screen;

	rk_fb_get_prmry_screen(&screen);
	if(screen.lcdc_id == 1)
		hdmi_drv->lcdc = rk_get_lcdc_drv("lcdc0");
	else
		hdmi_drv->lcdc = rk_get_lcdc_drv("lcdc1");
	if(hdmi_drv->lcdc == NULL)
	{
		dev_err(hdmi_drv->dev, "can not connect to video source lcdc\n");
		ret = -ENXIO;
		return ret;
	}

#ifdef SUPPORT_HDCP
	hdmi_drv->irq = INVALID_GPIO;
#endif

	hdmi_sys_init(hdmi_drv);
	hdmi_drv->xscale = 100;
	hdmi_drv->yscale = 100;
	hdmi_drv->insert = cat66121_hdmi_sys_insert;
	hdmi_drv->remove = cat66121_hdmi_sys_remove;
	hdmi_drv->control_output = cat66121_hdmi_sys_enalbe_output;
	hdmi_drv->config_video = cat66121_hdmi_sys_config_video;
	hdmi_drv->config_audio = cat66121_hdmi_sys_config_audio;
	hdmi_drv->detect_hotplug = cat66121_hdmi_sys_detect_hpd;
	hdmi_drv->read_edid = cat66121_hdmi_sys_read_edid;

	#ifdef CONFIG_HAS_EARLYSUSPEND
	hdmi_drv->early_suspend.suspend = hdmi_early_suspend;
	hdmi_drv->early_suspend.resume = hdmi_early_resume;
	hdmi_drv->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 10;
	register_early_suspend(&hdmi_drv->early_suspend);
	#endif

	hdmi_register_display_sysfs(hdmi_drv, NULL);

	#ifdef CONFIG_SWITCH
	hdmi_drv->switch_hdmi.name="hdmi";
	switch_dev_register(&(hdmi_drv->switch_hdmi));
	#endif

	spin_lock_init(&hdmi_drv->irq_lock);
	mutex_init(&hdmi_drv->enable_mutex);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id cat66121_dt_ids[] = {
	{.compatible = "ite,cat66121",},
	{}
};
MODULE_DEVICE_TABLE(of, cat66121_dt_ids);
#endif

static int cat66121_hdmi_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int rc = 0;

	printk("%s,line=%d\n", __func__,__LINE__);

	if (client->dev.of_node) {
		if (!of_match_device(cat66121_dt_ids, &client->dev)) {
			dev_err(&client->dev,"Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

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

	rk_hdmi_parse_dt(hdmi);
	//power on
	rk_hdmi_pwr_enable(hdmi);

	if(cat66121_detect_device()!=1){
		dev_err(hdmi->dev, "can't find it66121 device \n");
		rc = -ENXIO;
		goto err_request_lcdc;
	}

	cat66121_hdmi->plug_status = -1;
	rk_hdmi_drv_init(hdmi);
	cat66121_hdmi_sys_init(hdmi);

	hdmi->workqueue = create_singlethread_workqueue("hdmi");
	INIT_DELAYED_WORK(&(hdmi->delay_work), hdmi_work);

	if(gpio_is_valid(hdmi->irq)) {
		//cat66121_irq_work_func(NULL);
		if((rc = gpio_request(hdmi->irq, "hdmi gpio")) < 0)
		{
			dev_err(&client->dev, "fail to request gpio %d\n", hdmi->irq);
			goto err_request_lcdc;
		}

		cat66121_hdmi->gpio = hdmi->irq;
		//gpio_pull_updown(hdmi->irq, GPIOPullUp);	//TODO Daisen
		gpio_direction_input(hdmi->irq);
		hdmi->irq = gpio_to_irq(hdmi->irq);
		if(hdmi->irq <= 0) {
			dev_err(hdmi->dev, "failed to get hdmi irq resource (%d).\n", hdmi->irq);
			goto err_request_irq;
		}

		if((rc = request_threaded_irq(hdmi->irq, NULL ,cat66121_thread_interrupt, IRQF_TRIGGER_LOW | IRQF_ONESHOT, dev_name(&client->dev), hdmi)) < 0) 
		{
			dev_err(&client->dev, "fail to request hdmi irq\n");
			goto err_request_irq;
		}
	}else{
		cat66121_hdmi->workqueue = create_singlethread_workqueue("cat66121 irq");
		INIT_DELAYED_WORK(&(cat66121_hdmi->delay_work), cat66121_irq_work_func);
		cat66121_irq_work_func(NULL);
	}

#if defined(CONFIG_DEBUG_FS)
	{
		struct dentry *debugfs_dir = debugfs_create_dir("it66121", NULL);
		if (IS_ERR(debugfs_dir))
		{
			dev_err(&client->dev,"failed to create debugfs dir for it66121!\n");
		}
		else
			debugfs_create_file("hdmi", S_IRUSR,debugfs_dir,hdmi,&hdmi_reg_fops);
	}
#endif

	dev_info(&client->dev, "cat66121 hdmi i2c probe ok\n");
	
    return 0;
	
err_request_irq:
	gpio_free(hdmi->irq);
err_request_lcdc:
	kfree(hdmi);
	hdmi = NULL;
err_kzalloc_hdmi:
	kfree(cat66121_hdmi);
	cat66121_hdmi = NULL;
	dev_err(&client->dev, "cat66121 hdmi probe error\n");
	return rc;

}

static int cat66121_hdmi_i2c_remove(struct i2c_client *client)
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
        .of_match_table = of_match_ptr(cat66121_dt_ids),
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
