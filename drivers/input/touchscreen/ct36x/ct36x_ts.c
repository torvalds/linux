/* SPDX-License-Identifier: GPL-2.0 */
#include "ct36x_priv.h"
#include <linux/of_gpio.h>
#include <linux/async.h>

#include "core.c"
#include "ct360.c"
#include "ct363.c"

int inline ct36x_set_ops(struct ct36x_data *ts, int model)
{
	switch(model){
		case 360: ts->ops = &ct360_ops; break;
		case 363: ts->ops = &ct363_ops; break;
		case 365: ts->ops = &ct363_ops; break;
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


static void ct36x_ts_early_suspend(struct tp_device *tp_d)
{
	struct ct36x_data *ts = container_of(tp_d, struct ct36x_data, tp);

	ct36x_dbg(ts, "<%s> touchscreen suspend\n", CT36X_NAME);

	disable_irq_nosync(ts->irq);

	if(ts->ops->suspend)
		ts->ops->suspend(ts);
	
}

static void ct36x_ts_late_resume(struct tp_device *tp_d)
{
	struct ct36x_data *ts = container_of(tp_d, struct ct36x_data, tp);

	ct36x_dbg(ts, "<%s> tochscreen resume\n", CT36X_NAME);
	if(ts->ops->resume)
		ts->ops->resume(ts);

	enable_irq(ts->irq);
}


static int ct36x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int orientation[4];
	int ret = 0, i;
	struct ct36x_data *ts = NULL;
	/*struct ct36x_platform_data *pdata = client->dev.platform_data;
	
	if(!pdata){
		dev_err(&client->dev, "no platform data\n");
		return -EINVAL;
	};*/
	struct device_node *np = client->dev.of_node;
	enum of_gpio_flags rst_flags;
	unsigned long irq_flags;
	u32 val;
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
		return -ENODEV;
	}

	ts = devm_kzalloc(&client->dev, sizeof(struct ct36x_data), GFP_KERNEL);
		if (ts == NULL) {
			dev_err(&client->dev, "alloc for struct rk_ts_data fail\n");
			return -ENOMEM;
		}
	
		if (!np) {
			dev_err(&client->dev, "no device tree\n");
			return -EINVAL;
		}
	if (of_property_read_u32(np, "max-x", &val)) {
		dev_err(&client->dev, "no max-x defined\n");
		return -EINVAL;
	}
	ts->x_max = val;
	if (of_property_read_u32(np, "max-y", &val)) {
		dev_err(&client->dev, "no max-y defined\n");
		return -EINVAL;
	}
	ts->y_max = val;

	printk("the ts->x_max is %d,ts->y_max is %d\n",ts->x_max,ts->y_max);
	if (of_property_read_u32(np, "ct-model", &val)) {
		dev_err(&client->dev, "no ct-model defined\n");
		return -EINVAL;
	}
	ts->model = val;//pdata->model;
	flag_ct36x_model = ts->model;
	//ts->x_max = pdata->x_max;
	//ts->y_max = pdata->y_max;
	//ts->rst_io = pdata->rst_io;
	//ts->irq_io = pdata->irq_io;
	ts->irq_io.gpio = of_get_named_gpio_flags(np, "touch-gpio", 0, (enum of_gpio_flags *)&irq_flags);
	ts->rst_io.gpio = of_get_named_gpio_flags(np, "reset-gpio", 0, &rst_flags);

	//printk("the irq_flags is %ld,rst_flags is %d\n",irq_flags,rst_flags);
	
	ret = of_property_read_u32_array(np, "orientation",orientation,4);
	if (ret < 0)
		return ret;
	for(i = 0; i < 4; i++)
		ts->orientation[i] = orientation[i];

	ts->client = client;
	ts->dev = &client->dev;

	i2c_set_clientdata(client, ts);

	ret = ct36x_set_ops(ts, ts->model);
	if(ret < 0){
		dev_err(ts->dev, "Failed to set ct36x ops\n");
		goto err_ct36x_set_ops;
	}

	if (gpio_is_valid(ts->rst_io.gpio)) {
					ts->rst_io.active_low = (rst_flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
					//printk("the ts->rst_io.active_low is %d ========\n",ts->rst_io.active_low);
					ret = devm_gpio_request_one(&client->dev, ts->rst_io.gpio, (rst_flags & OF_GPIO_ACTIVE_LOW) ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW, "ct363 reset pin");
					if (ret != 0) {
						dev_err(&client->dev, "ct363 gpio_request error\n");
						return -EIO;
					}
				//msleep(100);
				} else {
					dev_info(&client->dev, "reset pin invalid\n");
				}
				
	if (gpio_is_valid(ts->irq_io.gpio)) {
					ts->irq_io.active_low = (irq_flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
					ret = devm_gpio_request_one(&client->dev, ts->irq_io.gpio, (irq_flags & OF_GPIO_ACTIVE_LOW) ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW, "ct363 irq pin");
					if (ret != 0) {
						dev_err(&client->dev, "ct363 gpio_request error\n");
						return -EIO;
					}
			}



	if(ts->ops->init){
		ret = ts->ops->init(ts);
		if(ret < 0){
			dev_err(ts->dev, "Failed to init ct36x chip\n");
			goto err_ct36x_init_chip;
		}
	}

	ts->input = devm_input_allocate_device(&ts->client->dev);
	if(!ts->input){
		ret = -ENODEV;
		dev_err(ts->dev, "Failed to allocate input device\n");
		goto err_input_allocate_device;
	}

	ts->input->name = CT36X_NAME;
	ts->input->dev.parent = &client->dev;
	set_bit(EV_ABS, ts->input->evbit);
	set_bit(INPUT_PROP_DIRECT, ts->input->propbit);
	input_mt_init_slots(ts->input, ts->point_num,0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, ts->x_max, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, ts->y_max, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);

	ret = input_register_device(ts->input);
	if(ret < 0){
		dev_err(ts->dev, "Failed to register input device\n");
		goto err_input_register_devcie;
	}

	
       ts->tp.tp_resume = ct36x_ts_late_resume;
       ts->tp.tp_suspend = ct36x_ts_early_suspend;
       tp_register_fb(&ts->tp);

	ts->irq = gpio_to_irq(ts->irq_io.gpio);
	if (ts->irq)
		{
			ret = devm_request_threaded_irq(&client->dev, ts->irq, NULL, ct36x_irq_handler, irq_flags | IRQF_ONESHOT, client->name, ts);
			if (ret != 0) {
				printk(KERN_ALERT "Cannot allocate ts INT!ERRNO:%d\n", ret);
				goto err_request_threaded_irq;
			}
			disable_irq(ts->irq);
		}

/*	ret = request_threaded_irq(ts->irq, NULL, ct36x_irq_handler, IRQF_TRIGGER_FALLING|IRQF_ONESHOT, CT36X_NAME, ts);
	if(ret < 0){
		dev_err(ts->dev, "Failed to request threaded irq\n");
		goto err_request_threaded_irq;
	}
*/		
	dev_info(ts->dev, "CT363 Successfully initialized\n");
	return 0;
err_request_threaded_irq:
	tp_unregister_fb(&ts->tp);
err_input_register_devcie:
err_input_allocate_device:
	if(ts->ops->deinit)
		ts->ops->deinit(ts);
err_ct36x_init_chip:
err_ct36x_set_ops:
	i2c_set_clientdata(client, NULL);
	return ret;
}

static int ct36x_ts_remove(struct i2c_client *client)
{
	struct ct36x_data *ts = i2c_get_clientdata(client);
	
	if(ts->ops->deinit)
		ts->ops->deinit(ts);
	//unregister_early_suspend(&ts->early_suspend);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id ct36x_ts_id[] = {
	{ CT36X_NAME, 0 },
	{ }
};
static struct of_device_id ct36x_ts_dt_ids[] = {
	{ .compatible = "ct,ct36x" },
	{ }
};

static struct i2c_driver ct36x_ts_driver = {
	.probe      = ct36x_ts_probe,
	.remove     = ct36x_ts_remove,
	.id_table   = ct36x_ts_id,
	.driver = {
		.owner	= THIS_MODULE, 
		.name	= CT36X_NAME,
		.of_match_table = of_match_ptr(ct36x_ts_dt_ids),
	},
};

static void __init ct36x_ts_init_async(void *unused, async_cookie_t cookie)
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
	i2c_add_driver(&ct36x_ts_driver);
}

static int __init ct36x_ts_init(void)
{
	async_schedule(ct36x_ts_init_async, NULL);
	return 0;
}

static void __exit ct36x_ts_exit(void)
{
	i2c_del_driver(&ct36x_ts_driver);
}

module_init(ct36x_ts_init);
module_exit(ct36x_ts_exit);

MODULE_DESCRIPTION("CT36X Touchscreens Driver");
MODULE_LICENSE("GPL");

