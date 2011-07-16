#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#include "linux/anx7150.h"
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

static int rk29_hdmi_enter(struct anx7150_dev_s *dev)
{
	if(dev->rk29_output_status == RK29_OUTPUT_STATUS_LCD) {
		dev->hdmi->resolution = dev->resolution_set;
		if(hdmi_switch_fb(dev->hdmi, 1) < 0)
			return -1;
		dev->rk29_output_status = RK29_OUTPUT_STATUS_HDMI;
	}
	return 0;
}
static int rk29_hdmi_exit(struct anx7150_dev_s *dev)
{
	if(dev->rk29_output_status == RK29_OUTPUT_STATUS_HDMI) {
		dev->hdmi->resolution = dev->resolution_set;
		if(hdmi_switch_fb(dev->hdmi, 0) < 0)
			return -1;
		dev->rk29_output_status = RK29_OUTPUT_STATUS_LCD;
	}
	return 0;
}

static int anx7150_display_on(struct hdmi* hdmi)
{
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);

	rk29_hdmi_enter(&anx->dev);
	hdmi->display_on = HDMI_ENABLE;
	anx->dev.hdmi_enable = HDMI_ENABLE;
	anx->dev.parameter_config = 1;
	hdmi_dbg(hdmi->dev, "hdmi display on\n");
	return 0;
}
static int anx7150_display_off(struct hdmi* hdmi)
{
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);
	
	rk29_hdmi_exit(&anx->dev);
	hdmi->display_on = HDMI_DISABLE;
	anx->dev.hdmi_enable = HDMI_DISABLE;
	anx->dev.parameter_config = 1;
	hdmi_dbg(hdmi->dev, "hdmi display off\n");
	return 0;
}
static int anx7150_set_param(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);

	anx->dev.resolution_set = hdmi->resolution;
	anx->dev.i2s_Fs = hdmi->audio_fs;
	anx->dev.hdcp_enable = hdmi->hdcp_on;
	anx->dev.hdmi_auto_switch = hdmi->auto_switch;
	anx->dev.parameter_config = 1;

	return 0;
}
static int anx7150_core_init(struct hdmi *hdmi)
{
	//struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);

	return 0;
}
static irqreturn_t anx7150_detect_irq(int irq, void *dev_id)
{
	//struct hdmi *hdmi = (struct hdmi *)dev_id;

    return IRQ_HANDLED;
}
#if 1//eboda zlj add for test 110518
struct anx7150_dev_s *anx7150_dev;
int anx7150_get_output_status(void)
{
    return anx7150_dev->rk29_output_status;
}
#endif
void anx7150_task(struct anx7150_pdata *anx)
{
	int state;
	int ret;
	
	//anx->dev.anx7150_detect = anx7150_detect_device(anx);
	if(anx->dev.anx7150_detect == 0)
		goto out;
	
	state = ANX7150_Get_System_State();

	if(anx->dev.parameter_config){
		if(state > WAIT_HDMI_ENABLE)
			state = WAIT_HDMI_ENABLE;
		anx->dev.parameter_config = 0;
		anx->dev.fb_switch_state = 1;
	}
	if(anx->dev.hdmi_enable == HDMI_DISABLE && anx->dev.hdmi_auto_switch == HDMI_DISABLE){
		//if(state > WAIT_HDMI_ENABLE)
			state = HDMI_INITIAL;
	}

	state = ANX7150_Interrupt_Process(anx, state);

	switch(state){
	case HDMI_INITIAL:
		if(anx->dev.hdmi_auto_switch)
			rk29_hdmi_exit(&anx->dev);
		ANX7150_API_Initial(anx->client);
		state = WAIT_HOTPLUG;
		if(anx->dev.hdmi_auto_switch)
			anx->dev.rate = 1;
		else
			anx->dev.rate = 100;
		break;
		
	case WAIT_HOTPLUG:
		if(anx->dev.hdmi_auto_switch)
			rk29_hdmi_exit(&anx->dev);
		if(anx->dev.HPD_status){
			anx7150_plug(anx->client);
			hdmi_changed(anx->dev.hdmi, 1);
			state = READ_PARSE_EDID;
		}
		if(anx->dev.hdmi_auto_switch)
			anx->dev.rate = 50;
		else
			anx->dev.rate = 100;
		break;
		
	case READ_PARSE_EDID:
		ret = ANX7150_Parse_EDID(anx->client,&anx->dev);
		if(ret != 0){
			dev_err(&anx->client->dev, "Parse_EDID err, ret=%d\n", ret);
		}

		state = WAIT_RX_SENSE;
		if(anx->dev.hdmi_auto_switch)
			anx->dev.rate = 50;
		else
			anx->dev.rate = 100;

		break;
		
	case WAIT_RX_SENSE:
		if(ANX7150_GET_SENSE_STATE(anx->client) == 1){
			hdmi_dbg(&anx->client->dev, "reciver active\n");
			state = WAIT_HDMI_ENABLE;
			anx->dev.reciver_status = HDMI_RECIVER_ACTIVE;
			hdmi_changed(anx->dev.hdmi, 1);
		}

		if(anx->dev.hdmi_auto_switch)
			anx->dev.rate = 50;
		else
			anx->dev.rate = 100;

		break;

	case WAIT_HDMI_ENABLE:
		if(!anx->dev.hdmi_enable && anx->dev.hdmi_auto_switch)
			rk29_hdmi_exit(&anx->dev);
		if(anx->dev.hdmi_enable && 
			(anx->dev.hdmi_auto_switch || anx->init ||anx->dev.parameter_config)) {
			rk29_hdmi_enter(&anx->dev);
			anx->init = 0;
		}
		
		/*
		if(1 || anx->dev.rk29_output_status == RK29_OUTPUT_STATUS_HDMI){
			state = SYSTEM_CONFIG;
			anx->dev.rate = 1;
		}
		*/
		state = SYSTEM_CONFIG;
		if(anx->dev.hdmi_auto_switch)
			anx->dev.rate = 50;
		else
			anx->dev.rate = 100;

		break;
		
	case SYSTEM_CONFIG:
		anx->dev.resolution_real = ANX7150_Get_Optimal_resolution(anx->dev.resolution_set);
		HDMI_Set_Video_Format(anx->dev.resolution_real);
		HDMI_Set_Audio_Fs(anx->dev.i2s_Fs);
		ANX7150_API_HDCP_ONorOFF(anx->dev.hdcp_enable);
		ANX7150_API_System_Config();
		state = CONFIG_VIDEO;

		anx->dev.rate = 1;
		if(anx->dev.fb_switch_state && anx->dev.rk29_output_status == RK29_OUTPUT_STATUS_HDMI) {
			anx->dev.rk29_output_status = RK29_OUTPUT_STATUS_LCD;
			rk29_hdmi_enter(&anx->dev);
			anx->dev.fb_switch_state = 0;
		}
		break;
		
	case CONFIG_VIDEO:
		if(ANX7150_Config_Video(anx->client) == 0){
			if(ANX7150_GET_RECIVER_TYPE() == 1)
				state = CONFIG_AUDIO;
			else
				state = HDCP_AUTHENTICATION;

			anx->dev.rate = 50;
		}

		anx->dev.rate = 10;
		break;
		
	case CONFIG_AUDIO:
		ANX7150_Config_Audio(anx->client);
		state = CONFIG_PACKETS;
		anx->dev.rate = 1;
		break;
		
	case CONFIG_PACKETS:
		ANX7150_Config_Packet(anx->client);
		state = HDCP_AUTHENTICATION;
		anx->dev.rate = 1;
		break;
		
	case HDCP_AUTHENTICATION:
		ANX7150_HDCP_Process(anx->client);
		state = PLAY_BACK;
		anx->dev.rate = 100;
		break;
		
	case PLAY_BACK:
		ret = ANX7150_PLAYBACK_Process();
		if(ret == 1){
			state = CONFIG_PACKETS;
			anx->dev.rate = 1;
		}

		anx->dev.rate = 100;
		break;
	
	default:
		state = HDMI_INITIAL;
		anx->dev.rate = 100;
		break;
	}

	if(state != ANX7150_Get_System_State()){
		ANX7150_Set_System_State(anx->client, state);
	}

out:
	return;
}

static void anx7150_work_func(struct work_struct * work)
{
	struct anx7150_dev_s *dev = container_of((void *)work, struct anx7150_dev_s, delay_work);
	struct hdmi *hdmi = dev->hdmi;
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);

	anx7150_task(anx);
/*
	if(dev->hdmi_auto_switch)
	{
		if(dev->HPD_status == HDMI_RECIVER_PLUG)
		{
			rk29_hdmi_enter(dev);
		}
		else
		{
			rk29_hdmi_exit(dev);
		}
	}
	else
	{
		if(dev->hdmi_enable)
		{
			rk29_hdmi_enter(dev);
		}
		else
		{
			rk29_hdmi_exit(dev);
		}
	}
*/
	if(dev->anx7150_detect)
	{
		queue_delayed_work(dev->workqueue, &dev->delay_work, dev->rate);
	}
	else
	{
		hdmi_dbg(hdmi->dev, "ANX7150 not exist!\n");
		rk29_hdmi_exit(dev);
	}
	return;
}
static int anx7150_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int rc = 0;
	struct hdmi *hdmi = NULL;
	struct anx7150_pdata *anx = NULL;

	hdmi = kzalloc(sizeof(struct hdmi), GFP_KERNEL);
    if (!hdmi)
    {
        dev_err(&client->dev, "no memory for state\n");
        return -ENOMEM;
    }
	anx = kzalloc(sizeof(struct anx7150_pdata), GFP_KERNEL);
	if(!anx)
	{
        dev_err(&client->dev, "no memory for state\n");
        goto err_kzalloc_anx;
    }
	anx->client = client;
	anx->dev.anx7150_detect = 0;
	anx->dev.resolution_set = HDMI_DEFAULT_RESOLUTION;
	anx->dev.i2s_Fs = HDMI_I2S_DEFAULT_Fs;
	anx->dev.hdmi_enable = HDMI_ENABLE;
	anx->dev.hdmi_auto_switch = HDMI_AUTO_SWITCH;
	anx->dev.reciver_status = HDMI_RECIVER_INACTIVE;
	anx->dev.HPD_status = HDMI_RECIVER_UNPLUG;
	anx->dev.HPD_change_cnt = 0;
	anx->dev.rk29_output_status = RK29_OUTPUT_STATUS_LCD;
	anx->dev.hdcp_enable = ANX7150_HDCP_EN;
	anx->dev.rate = 100;

	anx->init = 1;

	anx->dev.workqueue = create_singlethread_workqueue("ANX7150_WORKQUEUE");
	INIT_DELAYED_WORK(&anx->dev.delay_work, anx7150_work_func);
	
	hdmi->display_on = anx->dev.hdmi_enable;
	hdmi->auto_switch = anx->dev.hdmi_auto_switch;
	hdmi->hdcp_on = anx->dev.hdcp_enable;
	hdmi->audio_fs = anx->dev.i2s_Fs;
	hdmi->resolution = anx->dev.resolution_set;
	hdmi->dev = &client->dev;
	hdmi->hdmi_display_on = anx7150_display_on;
	hdmi->hdmi_display_off = anx7150_display_off;
	hdmi->hdmi_set_param = anx7150_set_param;
	hdmi->hdmi_core_init = anx7150_core_init;
	
	if((rc = hdmi_register(&client->dev, hdmi)) < 0)
	{
		dev_err(&client->dev, "fail to register hdmi\n");
		goto err_hdmi_register;
	}

    if((rc = gpio_request(client->irq, "hdmi gpio")) < 0)
    {
        dev_err(&client->dev, "fail to request gpio %d\n", client->irq);
        goto err_request_gpio;
    }

    anx->irq = gpio_to_irq(client->irq);
	anx->gpio = client->irq;

	anx->dev.hdmi = hdmi;
	hdmi_set_privdata(hdmi, anx);

	i2c_set_clientdata(client, anx);
    gpio_pull_updown(client->irq,GPIOPullUp);
	
    if((rc = request_irq(anx->irq, anx7150_detect_irq,IRQF_TRIGGER_FALLING,NULL,hdmi)) <0)
    {
        dev_err(&client->dev, "fail to request hdmi irq\n");
        goto err_request_irq;
    }
	anx->dev.anx7150_detect = anx7150_detect_device(anx);
	if(anx->dev.anx7150_detect) {
		ANX7150_API_Initial(client);
		queue_delayed_work(anx->dev.workqueue, &anx->dev.delay_work, 200);
	}
    dev_info(&client->dev, "anx7150 i2c probe ok\n");
    return 0;
	
err_request_irq:
	gpio_free(client->irq);
err_request_gpio:
	hdmi_unregister(hdmi);
err_hdmi_register:
	destroy_workqueue(anx->dev.workqueue);
	kfree(anx);
	anx = NULL;
err_kzalloc_anx:
	kfree(hdmi);
	hdmi = NULL;
	return rc;

}

static int __devexit anx7150_i2c_remove(struct i2c_client *client)
{
	struct anx7150_pdata *anx = (struct anx7150_pdata *)i2c_get_clientdata(client);
	struct hdmi *hdmi = anx->dev.hdmi;

	free_irq(anx->irq, NULL);
	gpio_free(client->irq);
	hdmi_unregister(hdmi);
	destroy_workqueue(anx->dev.workqueue);
	kfree(anx);
	anx = NULL;
	kfree(hdmi);
	hdmi = NULL;
		
	hdmi_dbg(hdmi->dev, "%s\n", __func__);
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


