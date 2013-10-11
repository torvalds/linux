/*
	Copyright (c) 2010 by Rockchip.
*/
#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#include <linux/wakelock.h>
#endif
#include <linux/scaler-core.h>

struct scaler_chip_dev *chip = NULL;
extern char *scaler_input_name[];


//enbale chip to process image
static void set_cur_inport(void) 
{
	struct scaler_input_port *iport = NULL;
	
	list_for_each_entry(iport, &chip->iports, next) {
		if (iport->id == chip->cur_inport_id) {
			gpio_set_value(iport->led_gpio, GPIO_HIGH);
		}else {
			gpio_set_value(iport->led_gpio, GPIO_LOW);
		}
	}

	//pc
	if (chip->cur_inport_id == 2)
		gpio_set_value(RK30_PIN0_PB4, GPIO_LOW);
	else
	//rk
		gpio_set_value(RK30_PIN0_PB4, GPIO_HIGH);
}

static int parse_cmd(unsigned int cmd, unsigned long arg)
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

static int test_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct scaler_platform_data  *pdata = client->dev.platform_data;

	if (!pdata) {
		printk("%s: client private data not define\n", __func__);
		return -1;
	}

	chip = alloc_scaler_chip();
	if (!chip) {
		printk("%s: alloc scaler chip memory failed.\n", __func__);
		return -1;
	}
	chip->client = client;
	memcpy((void *)chip->name, (void *)client->name, (strlen(client->name) + 1));
	//implement parse cmd function
	init_scaler_chip(chip, pdata);
	chip->parse_cmd = parse_cmd;

	//vga 5v en
	if (!gpio_request(RK30_PIN3_PD7, NULL))
		gpio_direction_output(RK30_PIN3_PD7, GPIO_HIGH); 
	else
		printk("%s: request vga5ven power gpio failed\n", __func__);
	msleep(20);

	//power
	if (!gpio_request(RK30_PIN2_PD7, NULL))
		gpio_direction_output(RK30_PIN2_PD7, GPIO_HIGH); 
	else
		printk("%s: request vga power gpio failed\n", __func__);
	msleep(20);

	//vga sel
	if (!gpio_request(RK30_PIN0_PB4, NULL))
		gpio_direction_output(RK30_PIN0_PB4, GPIO_HIGH); //rk output
	else
		printk("%s: request vga switch gpio failed\n", __func__);
	msleep(20);

	//
	if (!gpio_request(RK30_PIN1_PD6, NULL))
		gpio_direction_output(RK30_PIN1_PD6, GPIO_HIGH); 
	else
		printk("%s: request XNN223_PWN gpio failed\n", __func__);
	msleep(20);

	//register
	register_scaler_chip(chip);

	return 0;
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

