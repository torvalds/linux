/* SPDX-License-Identifier: GPL-2.0 */
/*
	Copyright (c) 2010 by Rockchip.
*/
#include <linux/module.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <linux/scaler-core.h>

struct scaler_chip_dev *chip = NULL;

//enbale chip to process image
static void set_cur_inport(void) 
{
	struct scaler_input_port *iport = NULL;
	int ddc_sel_level = -1;
	
	list_for_each_entry(iport, &chip->iports, next) {

		if (iport->id == chip->cur_inport_id) {
			if (iport->id == 1)
				ddc_sel_level = chip->pdata->ddc_sel_level;
			else
				ddc_sel_level = !chip->pdata->ddc_sel_level;
			if (chip->pdata->ddc_sel_gpio > 0)
				gpio_direction_output(chip->pdata->ddc_sel_gpio, ddc_sel_level);  
			gpio_set_value(iport->led_gpio, GPIO_HIGH);
		}else {
			gpio_set_value(iport->led_gpio, GPIO_LOW);
		}
	}
}

static int test_parse_cmd(unsigned int cmd, unsigned long arg)
{
	printk("test: parse scaler cmd %u\n",cmd);

	switch (cmd) {
		case SCALER_IOCTL_SET_CUR_INPUT:
			set_cur_inport();
			break;
		default:
			break;
	}

	return 0;
}

static void test_start(void)
{
	scaler_switch_default_screen();
}

static int test_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct scaler_platform_data  *pdata = client->dev.platform_data;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->adapter->dev, "%s failed ENODEV\n\n", __func__);
		return -ENODEV;
	}

	if (!pdata) {
		printk("%s: client private data not define\n", __func__);
		return -1;
	}

	scaler_init_platform(pdata);

	chip = alloc_scaler_chip();
	if (!chip) {
		printk("%s: alloc scaler chip memory failed.\n", __func__);
		return -1;
	}else {
		chip->client = client;
		chip->pdata = pdata;
		memcpy((void *)chip->name, (void *)client->name, (strlen(client->name) + 1));
	}

	if (init_scaler_chip(chip, pdata) != 0)
		goto err;
	//implement parse cmd function
	chip->parse_cmd = test_parse_cmd;
	chip->start = test_start;

	//register
	if (register_scaler_chip(chip) != 0)
		goto err;

	return 0;
err:
	free_scaler_chip(chip);
	return -1;
}

static int test_i2c_remove(struct i2c_client *client)
{

	printk("%s: \n", __func__);
	unregister_scaler_chip(chip);
	free_scaler_chip(chip);
	chip = NULL;
	return 0;
}


static const struct i2c_device_id test_i2c_id[] ={
	{"aswitch", 0}, 
	{}
};
MODULE_DEVICE_TABLE(i2c, test_i2c_id);

static struct i2c_driver test_i2c_driver = {
	.driver = {
		.name = "aswitch"
	},
	.probe = test_i2c_probe,
	.remove = test_i2c_remove,
	.id_table = test_i2c_id,
};

static int __init test_init(void)
{
	int ret = 0;
	printk("%s: \n", __func__);

	ret = i2c_add_driver(&test_i2c_driver);
	if(ret < 0){
		printk("%s, register i2c device, error\n", __func__);
		return ret;
	}

	return 0;
}

static void __exit test_exit(void)
{
	printk("%s: \n", __func__);
}

module_init(test_init);
module_exit(test_exit);

