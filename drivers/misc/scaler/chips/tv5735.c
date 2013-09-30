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
#include "../scaler-core.h"

#define TV_DEV_NAME "trueview"
#define TV_I2C_RATE  (100*1000)

struct scaler_chip_dev *chip = NULL;

/***      I2c operate    ***/
static int tv_select_bank(struct i2c_client *client, char bank)
{
	int ret;
	u8 sel_bank = 0xf0;

	ret = i2c_master_reg8_send(client, sel_bank, &bank, 1, TV_I2C_RATE);
	if (ret != 1)
		printk("%s: write bank = %d failed.\n", __func__, bank);
	return ret;

}

static int tv_read_single_data(struct i2c_client *client, char reg)
{
	int ret;
	unsigned char val;

	ret = i2c_master_reg8_recv(client, reg, &val, 1, TV_I2C_RATE);
	if (ret != 1)
		printk("%s: read reg8 value error:%d\n", __func__, ret);

	return val;
}

static int tv_write_single_data(struct i2c_client *client, char reg, char value)
{
	int ret;
	ret = i2c_master_reg8_send(client, reg, &value, 1, TV_I2C_RATE);
	if (ret != 1)
		printk("%s: write reg = %d failed.\n", __func__, reg);
	return ret;
}

char test[] = "Test";
int read_dev(unsigned short reg, int bytes, void *desc)
{
	int res = -1;
	if (desc) {
		memcpy(desc, test, 5);
		res = 5;
	}

	return res;
}

static int parse_cmd(unsigned int cmd, void *arg)
{
	printk("tv5735: parse scaler cmd %u\n",cmd);
	return 0;
}

//enbale chip to process image
static void start(void) 
{
}

extern char *scaler_input_name[];
static int tv_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct scaler_platform_data  *pdata = client->dev.platform_data;

	if (!pdata) {
		printk("%s: client private data not define\n", __func__);
		return -1;
	}else {
		printk("%s: <%s,%d>\n", __func__, scaler_input_name[pdata->default_in], pdata->in_leds_gpio[pdata->default_in]);
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
	//register
	register_scaler_chip(chip);

	return 0;
}

static int tv_i2c_remove(struct i2c_client *client)
{

	printk("%s: \n", __func__);
	unregister_scaler_chip(chip);
	free_scaler_chip(chip);
	chip = NULL;
	return 0;
}


static const struct i2c_device_id tv_i2c_id[] ={
	{"tv5735", 0}, {}
};
MODULE_DEVICE_TABLE(i2c, tv_i2c_id);

static struct i2c_driver tv_i2c_driver = {
	.id_table = tv_i2c_id,
	.driver = {
		.name = "tv5735"
	},
	.probe = tv_i2c_probe,
	.remove = tv_i2c_remove,
};

static int __init tv_init(void)
{
	int ret = 0;
	printk("%s: .\n", __func__);

	ret = i2c_add_driver(&tv_i2c_driver);
	if(ret < 0){
		printk("%s, register i2c device, error\n", __func__);
		return ret;
	}

	return 0;
}

static void __exit tv_exit(void)
{
	printk("%s: .\n", __func__);
}

module_init(tv_init);
module_exit(tv_exit);

