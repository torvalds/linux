#include "ct36x_priv.h"

#include "core.c"
#include "ct360.c"
#include "ct363.c"

int inline ct36x_set_ops(struct ct36x_data *ts, int model)
{
	switch(model){
		case 360: ts->ops = &ct360_ops; break;
		case 363: ts->ops = &ct363_ops; break;
		default: return -EINVAL;
	};

	return 0;
}

#ifndef CONFIG_CT36X_TS   //make modules
static int en = 0;
module_param(en, int, 0644);

static int model = -1;
module_param(model, int, 0644);

static int i2c = -1;
module_param(i2c, int, 0644);

static int addr = -1;
module_param(addr, int, 0644);

static int x_max = -1;
module_param(x_max, int, 0644);

static int y_max = -1;
module_param(y_max, int, 0644);

static int irq = -1;
module_param(irq, int, 0644);

static int rst = -1;
module_param(rst, int, 0644);

static int orientation[4] = {1, 0, 1, 0};
module_param_array(orientation, int, NULL, 0644);

static int ct36x_check_param(void)
{
	int i;

	if(en != 1)
		return -EINVAL;
	if(model < 0)
		return -EINVAL;
	if(i2c < 0)
		return -EINVAL;
	if(addr <= 0x00 || addr >=0x80)
		return -EINVAL;
	if(x_max <= 0 || y_max <= 0)
		return -EINVAL;
	for(i = 0; i < 4; i++){
		if(orientation[i] != 0 && orientation[i] != 1 && orientation[i] != -1)
			return -EINVAL;
	}

	return 0;
}

static struct ct36x_platform_data ct36x_pdata;

static struct i2c_board_info __initdata ct36x_info = {
	.type = CT36X_NAME,
	.flags = 0,
	.platform_data = &ct36x_pdata,
};

static int ct36x_add_client(void)
{
	int i;
	struct port_config ct36x_rst, ct36x_irq;

	ct36x_pdata.model = model;
	ct36x_pdata.x_max = x_max;
	ct36x_pdata.y_max = y_max;

	for(i = 0; i < 4; i++)
		ct36x_pdata.orientation = orientation[i];
	ct36x_rst = get_port_config(rst);
	ct36x_pdata.rst_io.gpio = ct36x_rst.gpio;
	ct36x_pdata.rst_io.active_low = ct36x_rst.io.active_low;

	ct36x_irq = get_port_config(irq);
	ct36x_pdata.irq_io.gpio = ct36x_rst.gpio;
	ct36x_pdata.irq_io.active_low = ct36x_rst.io.active_low;
	
	ct36x_info.addr = addr;

	return i2c_add_device(i2c, &ct36x_info);
}
#endif

static irqreturn_t ct36x_irq_handler(int irq, void *data)
{
	struct ct36x_data *ts = data;
      ct36x_dbg(ts, "----------- ct36x_irq_handler -----------\n");
	//disable_irq(ts->irq);
	if(ts->ops->report)
		ts->ops->report(ts);

	//enable_irq(ts->irq);

	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ct36x_ts_early_suspend(struct early_suspend *h)
{
	struct ct36x_data *ts = container_of(h, struct ct36x_data, early_suspend);

	ct36x_dbg(ts, "<%s> touchscreen suspend\n", CT36X_NAME);

	disable_irq_nosync(ts->irq);

	if(ts->ops->suspend)
		ts->ops->suspend(ts);
	
}

static void ct36x_ts_late_resume(struct early_suspend *h)
{
	struct ct36x_data *ts = container_of(h, struct ct36x_data, early_suspend);

	ct36x_dbg(ts, "<%s> tochscreen resume\n", CT36X_NAME);
	if(ts->ops->resume)
		ts->ops->resume(ts);

	enable_irq(ts->irq);
}
#endif

static int ct36x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0, i;
	struct ct36x_data *ts = NULL;
	struct ct36x_platform_data *pdata = client->dev.platform_data;

	if(!pdata){
		dev_err(&client->dev, "no platform data\n");
		return -EINVAL;
	};

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
		return -ENODEV;
	}

	ts = kzalloc(sizeof(struct ct36x_data), GFP_KERNEL);
	if(!ts){
		dev_err(&client->dev, "No memory for ct36x");
		return -ENOMEM;
	}
	ts->model = pdata->model;
	ts->x_max = pdata->x_max;
	ts->y_max = pdata->y_max;
	ts->rst_io = pdata->rst_io;
	ts->irq_io = pdata->irq_io;

	for(i = 0; i < 4; i++)
		ts->orientation[i] = pdata->orientation[i];

	ts->client = client;
	ts->dev = &client->dev;

	i2c_set_clientdata(client, ts);

	ret = ct36x_set_ops(ts, pdata->model);
	if(ret < 0){
		dev_err(ts->dev, "Failed to set ct36x ops\n");
		goto err_ct36x_set_ops;
	}

	if(ts->ops->init){
		ret = ts->ops->init(ts);
		if(ret < 0){
			dev_err(ts->dev, "Failed to init ct36x chip\n");
			goto err_ct36x_init_chip;
		}
	}

	ts->input = input_allocate_device();
	if(!ts->input){
		ret = -ENODEV;
		dev_err(ts->dev, "Failed to allocate input device\n");
		goto err_input_allocate_device;
	}

	ts->input->name = CT36X_NAME;
	ts->input->dev.parent = &client->dev;
	set_bit(EV_ABS, ts->input->evbit);
	set_bit(INPUT_PROP_DIRECT, ts->input->propbit);
	input_mt_init_slots(ts->input, ts->point_num);
	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, pdata->y_max, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);

	ret = input_register_device(ts->input);
	if(ret < 0){
		dev_err(ts->dev, "Failed to register input device\n");
		goto err_input_register_devcie;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = ct36x_ts_early_suspend;
	ts->early_suspend.resume = ct36x_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
	ts->irq = gpio_to_irq(ts->irq_io.gpio);
	ret = request_threaded_irq(ts->irq, NULL, ct36x_irq_handler, IRQF_TRIGGER_FALLING|IRQF_ONESHOT, CT36X_NAME, ts);
	if(ret < 0){
		dev_err(ts->dev, "Failed to request threaded irq\n");
		goto err_request_threaded_irq;
	}
		
	dev_info(ts->dev, "CT363 Successfully initialized\n");
	return 0;
err_request_threaded_irq:
	unregister_early_suspend(&ts->early_suspend);
	input_unregister_device(ts->input);
err_input_register_devcie:
	input_free_device(ts->input);
err_input_allocate_device:
	if(ts->ops->deinit)
		ts->ops->deinit(ts);
err_ct36x_init_chip:
err_ct36x_set_ops:
	i2c_set_clientdata(client, NULL);
	kfree(ts);
	return ret;
}

static int ct36x_ts_remove(struct i2c_client *client)
{
	struct ct36x_data *ts = i2c_get_clientdata(client);
	
	free_irq(ts->irq, ts);
	if(ts->ops->deinit)
		ts->ops->deinit(ts);
	unregister_early_suspend(&ts->early_suspend);
	input_unregister_device(ts->input);
	input_free_device(ts->input);
	i2c_set_clientdata(client, NULL);
	kfree(ts);

	return 0;
}

static const struct i2c_device_id ct36x_ts_id[] = {
	{ CT36X_NAME, 0 },
	{ }
};
static struct i2c_driver ct36x_ts_driver = {
	.probe      = ct36x_ts_probe,
	.remove     = ct36x_ts_remove,
	.id_table   = ct36x_ts_id,
	.driver = {
		.owner	= THIS_MODULE, 
		.name	= CT36X_NAME,
	},
};

static int __init ct36x_ts_init(void)
{
#ifndef CONFIG_CT36X_TS   //make modules
	int ret = 0;

	ret = ct36x_check_param();
	if(ret < 0){
		pr_err("<%s> Param error, en: %d, model:%d, i2c: %d, addr: %d, x_max: %d, y_max: %d\n",
				CT36X_NAME, en, model, i2c, addr, x_max, y_max);
		return ret;
	}

	ret = ct36x_add_client();
	if(ret < 0){
		pr_err("<%s> Failed to add client, i2c: %d, addr: %d\n", CT36X_NAME, i2c, addr);
		return ret;
	}
#endif
	return i2c_add_driver(&ct36x_ts_driver);
}

static void __exit ct36x_ts_exit(void)
{
	i2c_del_driver(&ct36x_ts_driver);
}

module_init(ct36x_ts_init);
module_exit(ct36x_ts_exit);

MODULE_DESCRIPTION("CT36X Touchscreens Driver");
MODULE_LICENSE("GPL");

