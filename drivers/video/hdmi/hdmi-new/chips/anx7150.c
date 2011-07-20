#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hdmi-new.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <mach/iomux.h>



#include "anx7150.h"
#include "anx7150_hw.h"

int anx7150_i2c_read_p0_reg(struct i2c_client *client, char reg, char *val)
{
	client->addr = ANX7150_I2C_ADDR0;
	return i2c_master_reg8_recv(client, reg, val, 1, ANX7150_SCL_RATE) > 0? 0: -EINVAL;
}
int anx7150_i2c_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	client->addr = ANX7150_I2C_ADDR0;
	return i2c_master_reg8_send(client, reg, val, 1, ANX7150_SCL_RATE) > 0? 0: -EINVAL;
}
int anx7150_i2c_read_p1_reg(struct i2c_client *client, char reg, char *val)
{
	client->addr = ANX7150_I2C_ADDR1;
	return i2c_master_reg8_recv(client, reg, val, 1, ANX7150_SCL_RATE) > 0? 0: -EINVAL;
}
int anx7150_i2c_write_p1_reg(struct i2c_client *client, char reg, char *val)
{
	client->addr = ANX7150_I2C_ADDR1;
	return i2c_master_reg8_send(client, reg, val, 1, ANX7150_SCL_RATE) > 0? 0: -EINVAL;
}

static int anx7150_param_chg(struct anx7150_pdata *anx)
{
	int resolution_real;

	hdmi_set_spk(anx->hdmi->display_on);
	hdmi_set_backlight(!anx->hdmi->display_on);
	hdmi_switch_fb(anx->hdmi, anx->hdmi->display_on);
	resolution_real = ANX7150_Get_Optimal_resolution(anx->hdmi->resolution);
	HDMI_Set_Video_Format(resolution_real);
	HDMI_Set_Audio_Fs(anx->hdmi->audio_fs);
	ANX7150_API_HDCP_ONorOFF(anx->hdmi->hdcp_on);
	ANX7150_API_System_Config();
	ANX7150_Config_Video(anx->client);

	ANX7150_Config_Audio(anx->client);
	ANX7150_Config_Packet(anx->client);
	ANX7150_HDCP_Process(anx->client, anx->hdmi->display_on);
	ANX7150_PLAYBACK_Process();

	return 0;
}

static int anx7150_insert(struct hdmi *hdmi)
{
	int tmo = 10;
	struct anx7150_pdata *anx = hdmi_priv(hdmi);

	anx7150_plug(anx->client);
	if(ANX7150_Parse_EDID(anx->client,&anx->dev) < 0)
	{
		dev_info(&anx->client->dev, "parse EDID error\n");
		anx7150_unplug(anx->client);
		return -1;
	}
		
	while(--tmo && ANX7150_GET_SENSE_STATE(anx->client) != 1)
		mdelay(10);
	if(tmo <= 0)
	{
		anx7150_unplug(anx->client);
		return -1;
	}
	if(!hdmi->display_on)
		return 0;
	anx7150_param_chg(anx);
	return 0;
}
static int anx7150_remove(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);

	anx7150_unplug(anx->client);
	hdmi_set_spk(HDMI_DISABLE);
	hdmi_set_backlight(HDMI_ENABLE);
	hdmi_switch_fb(hdmi, HDMI_DISABLE);

	return 0;
}
static int anx7150_shutdown(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);
	
	anx7150_unplug(anx->client);

	return 0;
}
static int anx7150_display_on(struct hdmi* hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);

	hdmi->display_on = HDMI_ENABLE;
	hdmi_dbg(hdmi->dev, "hdmi display on\n");
	anx7150_param_chg(anx);
	return 0;
}
static int anx7150_display_off(struct hdmi* hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);

	hdmi->display_on = HDMI_DISABLE;
	anx->dev.hdmi_enable = HDMI_DISABLE;
	hdmi_dbg(hdmi->dev, "hdmi display off\n");
	anx7150_param_chg(anx);
	return 0;
}
static int anx7150_set_param(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);

	anx7150_param_chg(anx);
	return 0;
}

static int anx7150_hdmi_precent(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);

	return gpio_get_value(anx->client->irq)?0:1;
}
static struct hdmi_ops anx7150_ops = {
	.display_on = anx7150_display_on,
	.display_off = anx7150_display_off,
	.set_param = anx7150_set_param,
	.hdmi_precent = anx7150_hdmi_precent,
	.insert = anx7150_insert,
	.remove = anx7150_remove,
	.shutdown = anx7150_shutdown,
};
static irqreturn_t anx7150_detect_irq(int irq, void *dev_id);
static void anx7150_detect_work(struct work_struct *work)
{
	int ret = 0;
	struct anx7150_pdata *anx =  container_of(work, struct anx7150_pdata, work.work);

	free_irq(anx->irq, anx);
	ret = request_irq(anx->irq, anx7150_detect_irq,
		anx7150_hdmi_precent(anx->hdmi)? IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING,NULL,anx);
	dev_info(&anx->client->dev, "det = %d,hpd_status = %d\n", 
		gpio_get_value(anx->client->irq), anx7150_get_hpd(anx->client));
	anx->is_changed = 1;
	if(!anx->is_early_suspend)
		hdmi_changed(anx->hdmi, 0);
}

static irqreturn_t anx7150_detect_irq(int irq, void *dev_id)
{

	struct anx7150_pdata *anx = (struct anx7150_pdata *)dev_id;

	disable_irq_nosync(anx->irq);
	schedule_delayed_work(&anx->work, msecs_to_jiffies(200));

    return IRQ_HANDLED;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
static void anx7150_early_suspend(struct early_suspend *h)
{
	struct anx7150_pdata *anx = container_of(h,
							struct anx7150_pdata,
							early_suspend);
	dev_info(&anx->client->dev, "anx7150 enter early suspend\n");
	anx->is_early_suspend = 1;
	flush_delayed_work(&anx->work);
	if(anx->hdmi->display_on)
		hdmi_suspend(anx->hdmi);

	return;
}

static void anx7150_early_resume(struct early_suspend *h)
{
	int ret = 0;
	struct anx7150_pdata *anx = container_of(h,
							struct anx7150_pdata,
							early_suspend);
	dev_info(&anx->client->dev, "anx7150 exit early suspend\n");
	anx->is_early_suspend = 0;
	if(anx->hdmi->display_on)
		ret = hdmi_resume(anx->hdmi);
	return;

}
#endif

static int anx7150_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int ret = 0;
	struct hdmi *hdmi = NULL;
	struct anx7150_pdata *anx = NULL;

	hdmi = hdmi_register(sizeof(struct anx7150_pdata), &client->dev);
    if (!hdmi)
    {
        dev_err(&client->dev, "fail to register hdmi\n");
        return -ENOMEM;
    }
	hdmi->ops = &anx7150_ops;
	hdmi->display_on = HDMI_ENABLE;
	hdmi->auto_switch = HDMI_DISABLE;
	hdmi->hdcp_on = HDMI_DISABLE;
	hdmi->audio_fs = HDMI_I2S_DEFAULT_Fs;
	hdmi->resolution = HDMI_DEFAULT_RESOLUTION;
	
	anx = hdmi_priv(hdmi);
	anx->hdmi = hdmi;
	i2c_set_clientdata(client, anx);
	anx->client = client;

    if((ret = gpio_request(client->irq, "hdmi gpio")) < 0)
    {
        dev_err(&client->dev, "fail to request gpio %d\n", client->irq);
        goto err_hdmi_unregister;
    }
	gpio_pull_updown(client->irq,0);
	gpio_direction_input(client->irq);

    anx->irq = gpio_to_irq(client->irq);
	INIT_DELAYED_WORK(&anx->work, anx7150_detect_work);
    if((ret = request_irq(anx->irq, anx7150_detect_irq,
		anx7150_hdmi_precent(hdmi)?IRQF_TRIGGER_RISING:IRQF_TRIGGER_FALLING, NULL, anx)) <0)
    {
        dev_err(&client->dev, "fail to request hdmi irq\n");
        goto err_gpio_free;
    }
	if(anx7150_detect_device(anx) < 0)
	{
		dev_err(&client->dev, "anx7150 is not exist\n");
		ret = -EIO;
		goto err_free_irq;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	anx->early_suspend.suspend = anx7150_early_suspend;
	anx->early_suspend.resume = anx7150_early_resume;
	anx->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 1;
	register_early_suspend(&anx->early_suspend);
#endif
	anx->is_early_suspend = 0;
	anx->is_changed = 1;

	hdmi_changed(hdmi, 200);
    dev_info(&client->dev, "anx7150 i2c probe ok\n");
    return 0;
err_free_irq:
	free_irq(anx->irq, anx);
err_gpio_free:
	gpio_free(client->irq);
err_hdmi_unregister:
	hdmi_unregister(hdmi);
	anx = NULL;
	return ret;
}

static int __devexit anx7150_i2c_remove(struct i2c_client *client)
{
	struct anx7150_pdata *anx = (struct anx7150_pdata *)i2c_get_clientdata(client);
	struct hdmi *hdmi = anx->hdmi;

	free_irq(anx->irq, anx);
	gpio_free(client->irq);
	hdmi_unregister(hdmi);
	anx = NULL;
    return 0;
}
#if 0
static int anx7150_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct anx7150_pdata *anx = (struct anx7150_pdata *)i2c_get_clientdata(client);

	return hdmi_suspend(anx->hdmi);
}
static int anx7150_i2c_resume(struct i2c_client *client)
{
	int ret = 0;
	struct anx7150_pdata *anx = (struct anx7150_pdata *)i2c_get_clientdata(client);

	ret = hdmi_resume(anx->hdmi);
	return ret;
}
#endif
static const struct i2c_device_id anx7150_id[] = {
	{ "anx7150", 0 },
	{ }
};

static struct i2c_driver anx7150_i2c_driver  = {
    .driver = {
        .name  = "anx7150",
        .owner = THIS_MODULE,
    },
    .probe =    &anx7150_i2c_probe,
    .remove     = &anx7150_i2c_remove,
    //.suspend 	= &anx7150_i2c_suspend,
    //.resume		= &anx7150_i2c_resume,
    .id_table	= anx7150_id,
};


static int __init anx7150_init(void)
{
    return i2c_add_driver(&anx7150_i2c_driver);
}

static void __exit anx7150_exit(void)
{
    i2c_del_driver(&anx7150_i2c_driver);
}

//module_init(anx7150_init);
fs_initcall(anx7150_init);
module_exit(anx7150_exit);


