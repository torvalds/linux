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

static int anx7150_precent(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);

	return gpio_get_value(anx->client->irq)?0:1;
}
static int anx7150_param_chg(struct anx7150_pdata *anx)
{
	int resolution_real;

	hdmi_switch_fb(anx->hdmi, HDMI_ENABLE);
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

	if(anx->init == 1)
		return -1;

	anx7150_plug(anx->client);
	hdmi_dbg(&anx->client->dev, "parse edid\n");
	if(ANX7150_Parse_EDID(anx->client,&anx->dev) < 0)
	{
		dev_info(hdmi->dev, "parse EDID error\n");
		anx7150_unplug(anx->client);
		return -1;
	}
		
	while(--tmo && ANX7150_GET_SENSE_STATE(anx->client) != 1)
		mdelay(10);
	if(tmo <= 0)
	{
		anx7150_unplug(anx->client);
		dev_dbg(hdmi->dev, "get sense_state error\n");
		return -1;
	}
	hdmi_set_spk(HDMI_DISABLE);
	hdmi_set_backlight(HDMI_DISABLE);
	hdmi->scale = hdmi->scale_set;
	anx7150_param_chg(anx);
	return 0;
}
static int anx7150_remove(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);

	if(anx->init == 1)
		return -1;

	anx7150_unplug(anx->client);
	hdmi->scale = 100;
	hdmi_set_spk(HDMI_ENABLE);
	hdmi_switch_fb(hdmi, HDMI_DISABLE);
	hdmi_set_backlight(HDMI_ENABLE);

	return 0;
}

static int anx7150_set_param(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);
	if(anx->init == 1)
		return 0;

	anx7150_param_chg(anx);
	return 0;
}

static int anx7150_init(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_priv(hdmi);
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
	anx->init = 0;
	hdmi_changed(hdmi,1);

	return 0;
}
static void anx7150_init_work_func(struct work_struct *work)
{
    struct anx7150_pdata *anx = container_of(work, struct anx7150_pdata, work.work);

    if(anx!=NULL)
        anx7150_init(anx->hdmi);
    else
        printk("anx7150_init_work_func  err\n");
}
static struct hdmi_ops anx7150_ops = {
	.set_param = anx7150_set_param,
	.hdmi_precent = anx7150_precent,
	.insert = anx7150_insert,
	.remove = anx7150_remove,
	.init = anx7150_init,
};
#ifdef CONFIG_HAS_EARLYSUSPEND
static void anx7150_early_suspend(struct early_suspend *h)
{
	struct anx7150_pdata *anx = container_of(h,
							struct anx7150_pdata,
							early_suspend);
	dev_info(&anx->client->dev, "anx7150 enter early suspend\n");
	hdmi_suspend(anx->hdmi);
	return;
}

static void anx7150_early_resume(struct early_suspend *h)
{
	struct anx7150_pdata *anx = container_of(h,
							struct anx7150_pdata,
							early_suspend);
	dev_info(&anx->client->dev, "anx7150 exit early suspend\n");
	hdmi_resume(anx->hdmi);
	return;
}
#endif

static int anx7150_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int ret = 0;
	struct hdmi *hdmi = NULL;
	struct anx7150_pdata *anx = NULL;

	struct hdmi_platform_data *pdata = client->dev.platform_data;

	if(pdata && pdata->io_init)
		pdata->io_init();

	hdmi = hdmi_register(sizeof(struct anx7150_pdata), &client->dev);
    if (!hdmi)
    {
        dev_err(&client->dev, "fail to register hdmi\n");
        return -ENOMEM;
    }
	hdmi->ops = &anx7150_ops;
	hdmi->display_on = HDMI_DEFAULT_MODE;
	hdmi->hdcp_on = HDMI_DISABLE;
	hdmi->audio_fs = HDMI_I2S_DEFAULT_Fs;
	hdmi->resolution = HDMI_DEFAULT_RESOLUTION;
	hdmi->dual_disp = DUAL_DISP_CAP;
	hdmi->mode = DISP_ON_LCD;
	hdmi->scale = 100;
	hdmi->scale_set = 100;
	
	anx = hdmi_priv(hdmi);
	anx->init = 1;
	anx->hdmi = hdmi;
	i2c_set_clientdata(client, anx);
	anx->client = client;

    if((ret = gpio_request(client->irq, "hdmi gpio")) < 0)
    {
        dev_err(&client->dev, "fail to request gpio %d\n", client->irq);
        goto err_hdmi_unregister;
    }
	//gpio_pull_updown(client->irq,0);
	gpio_direction_input(client->irq);

	if(anx7150_detect_device(anx) < 0)
	{
		dev_err(&client->dev, "anx7150 is not exist\n");
		ret = -EIO;
		goto err_gpio_free;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	anx->early_suspend.suspend = anx7150_early_suspend;
	anx->early_suspend.resume = anx7150_early_resume;
	anx->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 1;
	register_early_suspend(&anx->early_suspend);
#endif
	anx7150_unplug(anx->client);
	INIT_DELAYED_WORK(&anx->work,anx7150_init_work_func);
	schedule_delayed_work(&anx->work, msecs_to_jiffies(2000));
    dev_info(&client->dev, "anx7150 i2c probe ok\n");
    return 0;
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

	gpio_free(client->irq);
	hdmi_unregister(hdmi);
	anx = NULL;
    return 0;
}
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
    .id_table	= anx7150_id,
};


static int __init anx7150_module_init(void)
{
    return i2c_add_driver(&anx7150_i2c_driver);
}

static void __exit anx7150_module_exit(void)
{
    i2c_del_driver(&anx7150_i2c_driver);
}

module_init(anx7150_module_init);
//fs_initcall(anx7150_module_init);
module_exit(anx7150_module_exit);


