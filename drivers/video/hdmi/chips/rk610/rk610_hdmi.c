#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/irq.h>

#include <linux/mfd/rk610_core.h>
#include "rk610_hdmi.h"
#include "rk610_hdmi_hw.h"


struct i2c_client *rk610_g_hdmi_client=NULL;
static bool hpd=0;

static void rk610_handler(struct work_struct *work)
{
	struct i2c_client *client = rk610_g_hdmi_client;
	if(client==NULL){
        printk(">>> %s client==NULL\n",__func__);
	}
	Rk610_hdmi_event_work(client,&hpd);
}

static DECLARE_DELAYED_WORK(rk610_irq_work, rk610_handler);
static int rk610_hdmi_precent(struct hdmi *hdmi)
{
    //struct rk610_hdmi_inf *rk610_hdmi = hdmi_priv(hdmi);
    schedule_delayed_work(&rk610_irq_work, msecs_to_jiffies(30));
    return hpd;
}

static int rk610_hdmi_param_chg(struct rk610_hdmi_inf *rk610_hdmi)
{
    int resolution_real;
    RK610_DBG(&rk610_hdmi->client->dev,"%s \n",__FUNCTION__);
    resolution_real = Rk610_Get_Optimal_resolution(rk610_hdmi->hdmi->resolution);
    rk610_hdmi->hdmi->resolution = resolution_real;
	hdmi_switch_fb(rk610_hdmi->hdmi, rk610_hdmi->hdmi->display_on);
	Rk610_hdmi_Set_Video(rk610_hdmi->hdmi->resolution);
    Rk610_hdmi_Set_Audio(rk610_hdmi->hdmi->audio_fs);
    Rk610_hdmi_Config_Done(rk610_hdmi->client);
	return 0;
}

static int rk610_hdmi_set_param(struct hdmi *hdmi)
{
	struct rk610_hdmi_inf *rk610_hdmi = hdmi_priv(hdmi);
	RK610_DBG(&rk610_hdmi->client->dev,"%s \n",__FUNCTION__);
	if(rk610_hdmi->init == 1)
		return 0;

	rk610_hdmi_param_chg(rk610_hdmi);
	return 0;
}
static int rk610_hdmi_insert(struct hdmi *hdmi)
{
	struct rk610_hdmi_inf *rk610_hdmi = hdmi_priv(hdmi);
    RK610_DBG(&rk610_hdmi->client->dev,"%s \n",__FUNCTION__);
	if(rk610_hdmi->init == 1)
		return -1;
	Rk610_hdmi_plug(rk610_hdmi->client);
	rk610_hdmi_param_chg(rk610_hdmi);
    hdmi_set_spk(HDMI_DISABLE);
    printk("rk610_hdmi_insert hdmi->display_on=%d\n",hdmi->display_on);
	hdmi->scale = hdmi->scale_set;
	return 0;
}
static int rk610_hdmi_remove(struct hdmi *hdmi)
{
	struct rk610_hdmi_inf *rk610_hdmi = hdmi_priv(hdmi);
    RK610_DBG(&rk610_hdmi->client->dev,"%s \n",__FUNCTION__);
	if(rk610_hdmi->init == 1)
		return -1;
	hdmi_set_spk(HDMI_ENABLE);
	hdmi_switch_fb(hdmi, HDMI_DISABLE);
	Rk610_hdmi_unplug(rk610_hdmi->client);
	printk("rk610_hdmi_remove hdmi->display_on=%d\n",hdmi->display_on);
	return 0;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
static void rk610_hdmi_early_suspend(struct early_suspend *h)
{
	struct rk610_hdmi_inf *rk610_hdmi = container_of(h,
							struct rk610_hdmi_inf,
							early_suspend);
	printk( "rk610_hdmi enter early suspend\n");
	hdmi_suspend(rk610_hdmi->hdmi);
	Rk610_hdmi_suspend(rk610_hdmi->client);
	return;
}

static void rk610_hdmi_early_resume(struct early_suspend *h)
{
	struct rk610_hdmi_inf *rk610_hdmi = container_of(h,
							struct rk610_hdmi_inf,
							early_suspend);
	printk("rk610_hdmi exit early suspend\n");
	hdmi_resume(rk610_hdmi->hdmi);
	Rk610_hdmi_resume(rk610_hdmi->client);
	return;
}
#endif
 
static int rk610_hdmi_init(struct hdmi *hdmi)
{
	struct rk610_hdmi_inf *rk610_hdmi = hdmi_priv(hdmi);
#ifdef CONFIG_HDMI_SAVE_DATA
    int hdmi_data = hdmi_get_data();
    if(hdmi_data<0){
    hdmi_set_data((hdmi->resolution&0x7)|((hdmi->scale&0x1f)<<3));
    }
    else{
    hdmi->resolution = hdmi_data&0x7;
    hdmi->scale_set= ((hdmi_data>>3)&0x1f) + MIN_SCALE;
    hdmi->scale = hdmi->scale_set;
    }
#endif  
	RK610_DBG(&rk610_hdmi->client->dev,"%s \n",__FUNCTION__);
	rk610_hdmi->init =0;
	Rk610_hdmi_init(rk610_hdmi->client);
    hdmi_changed(hdmi,1);
	Rk610_hdmi_Set_Video(hdmi->resolution);
	Rk610_hdmi_Set_Audio(hdmi->audio_fs);
    Rk610_hdmi_Config_Done(rk610_hdmi->client);
	return 0;
}
static struct hdmi_ops rk610_hdmi_ops = {
	.set_param = rk610_hdmi_set_param,
	.hdmi_precent = rk610_hdmi_precent,
	.insert = rk610_hdmi_insert,
	.remove = rk610_hdmi_remove,
	.init = rk610_hdmi_init,
};
#ifdef RK610_DEBUG
static int rk610_read_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_recv(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}

static int rk610_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_send(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}
static ssize_t rk610_show_reg_attrs(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{

	int i,size=0;
	char val;
	struct i2c_client *client=rk610_g_hdmi_client;

	for(i=0;i<256;i++)
	{
		rk610_read_p0_reg(client, i,  &val);
		if(i%16==0)
			size += sprintf(buf+size,"\n>>>rk610_hdmi %x:",i);
		size += sprintf(buf+size," %2x",val);
	}

	return size;
}
static ssize_t rk610_store_reg_attrs(struct device *dev,
						struct device_attribute *attr,
			 			const char *buf, size_t size)
{
	struct i2c_client *client=NULL;
	static char val=0,reg=0;
	client = rk610_g_hdmi_client;
	RK610_DBG(&client->dev,"/**********rk610 reg config******/");

	sscanf(buf, "%x%x", &val,&reg);
	RK610_DBG(&client->dev,"reg=%x val=%x\n",reg,val);
	rk610_write_p0_reg(client, reg,  &val);
	RK610_DBG(&client->dev,"val=%x\n",val);
	return size;
}

static struct device_attribute rk610_attrs[] = {
	__ATTR(reg_ctl, 0777,rk610_show_reg_attrs,rk610_store_reg_attrs),
};
#endif
#if 0
static irqreturn_t rk610_hdmi_interrupt(int irq, void *dev_id)
{
    struct hdmi *hdmi = (struct hdmi *)dev_id;
	unsigned long lock_flags = 0;
	printk("The rk610_hdmi interrupt handeler is working..\n");
	return IRQ_HANDLED;
}
#endif

static int 	rk610_hdmi_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int ret = 0;
	struct hdmi *hdmi = NULL;
	struct rk610_hdmi_inf *rk610_hdmi = NULL;

	struct hdmi_platform_data *pdata = client->dev.platform_data;
	rk610_g_hdmi_client = client;
	if(pdata && pdata->io_init)
	{
		printk("rk610_hdmi_i2c_probe io_init \n");
		pdata->io_init();
	}
	hdmi = hdmi_register(sizeof(struct rk610_hdmi_inf), &client->dev);
    if (!hdmi)
    {
        dev_err(&client->dev, "fail to register hdmi\n");
        return -ENOMEM;
    }
    	if(HDMI_SOURCE_DEFAULT == HDMI_SOURCE_LCDC0)
		hdmi->lcdc = rk_get_lcdc_drv("lcdc0");
	else
		hdmi->lcdc = rk_get_lcdc_drv("lcdc1");
	if(hdmi->lcdc == NULL)
	{
		dev_err(hdmi->dev, "can not connect to video source lcdc\n");
		ret = -ENXIO;
	}
	hdmi->ops = &rk610_hdmi_ops;
	hdmi->display_on = HDMI_DEFAULT_MODE;
	hdmi->hdcp_on = HDMI_DISABLE;
	hdmi->audio_fs = HDMI_I2S_DEFAULT_Fs;
	hdmi->resolution = HDMI_DEFAULT_RESOLUTION;
	hdmi->dual_disp = DUAL_DISP_CAP;
	hdmi->mode = DISP_ON_LCD;
	hdmi->scale = 100;
	hdmi->scale_set = 100;

	rk610_hdmi = hdmi_priv(hdmi);
	rk610_hdmi->init = 1;
	rk610_hdmi->hdmi = hdmi;
	i2c_set_clientdata(client, rk610_hdmi);
	rk610_hdmi->client = client;
	if((gpio_request(client->irq, "hdmi gpio")) < 0)
	    {
	        dev_err(&client->dev, "fail to request gpio %d\n", client->irq);
	        goto err_gpio_free;
	    }
    rk610_hdmi->irq = gpio_to_irq(client->irq);
	rk610_hdmi->gpio = client->irq;

	gpio_direction_input(client->irq);
	#if 0
	if((ret = request_irq(rk610_hdmi->irq, rk610_hdmi_interrupt, IRQ_TYPE_EDGE_RISING,client->name, hdmi))<0){
        RK610_ERR(&client->dev, "fail to request gpio %d\n", client->irq);
        goto err_gpio_free;
	}
	#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
	rk610_hdmi->early_suspend.suspend = rk610_hdmi_early_suspend;
	rk610_hdmi->early_suspend.resume = rk610_hdmi_early_resume;
	rk610_hdmi->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 1;
	register_early_suspend(&rk610_hdmi->early_suspend);
#endif
#ifdef RK610_DEBUG
	device_create_file(&(client->dev), &rk610_attrs[0]);
#endif
	rk610_hdmi_init(rk610_hdmi->hdmi);
    dev_info(&client->dev, "rk610_hdmi i2c probe ok\n");
    return 0;
err_gpio_free:
	gpio_free(client->irq);
err_hdmi_unregister:
	hdmi_unregister(hdmi);
	rk610_hdmi = NULL;
	return ret;
}

static int __devexit rk610_hdmi_i2c_remove(struct i2c_client *client)
{
	struct rk610_hdmi_inf *rk610_hdmi = (struct rk610_hdmi_inf *)i2c_get_clientdata(client);
	struct hdmi *hdmi = rk610_hdmi->hdmi;

	gpio_free(client->irq);
	hdmi_unregister(hdmi);
	rk610_hdmi = NULL;
    return 0;
}
static const struct i2c_device_id rk610_hdmi_id[] = {
	{ "rk610_hdmi", 0 },
	{ }
};

static struct i2c_driver rk610_hdmi_i2c_driver  = {
    .driver = {
        .name  = "rk610_hdmi",
    },
    .probe      = &rk610_hdmi_i2c_probe,
    .remove     = &rk610_hdmi_i2c_remove,
    .id_table	= rk610_hdmi_id,
};

static int __init rk610_hdmi_module_init(void)
{
    return i2c_add_driver(&rk610_hdmi_i2c_driver);
}

static void __exit rk610_hdmi_module_exit(void)
{
    i2c_del_driver(&rk610_hdmi_i2c_driver);
}

device_initcall_sync(rk610_hdmi_module_init);
module_exit(rk610_hdmi_module_exit);
