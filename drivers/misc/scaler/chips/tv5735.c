/* SPDX-License-Identifier: GPL-2.0 */
/*
	Copyright (c) 2010 by Rockchip.
*/
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <mach/gpio.h>
#include "tv_setting.h"


#define TV_DEV_NAME "trueview"
#define TV_I2C_RATE  (400*1000)

static struct timer_list timer;
static int vga_in_det = 0;
static int vga_offset = 0;
static struct scaler_chip_dev *chip = NULL;

/***      I2c operate    ***/
static int tv_select_bank(struct i2c_client *client, char bank)
{
	u8 sel_bank = 0xf0;
	return i2c_master_reg8_send(client, sel_bank, &bank, 1, TV_I2C_RATE);
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

static inline int tv_write_single_data(struct i2c_client *client, char reg, char value)
{
	return  i2c_master_reg8_send(client, reg, &value, 1, TV_I2C_RATE);
}

static void tv5735_init_reg(unsigned char *regs, int len)
{
	int i, ret = -1;
	char reg, val;
	
	if (chip == NULL)
		return;

	for (i = 0; i < len; i += 2) {
		reg = regs[i + 0];
		val = regs[i + 1];

		if (reg == 0xff && val == 0xff)
			break;

		//ret = i2c_master_reg8_send(chip->client, reg, &val, 1, TV_I2C_RATE);
		ret = tv_write_single_data(chip->client, reg, val);
		if (ret != 1)
			printk("%s: write reg = %d failed.\n", __func__, reg);
	}

}

static void set_cur_inport(void) 
{
	struct scaler_input_port *iport = NULL;
	int ddc_sel_level = -1;
	
	list_for_each_entry(iport, &chip->iports, next) {
		if (iport->id == chip->cur_inport_id) {
			gpio_set_value(iport->led_gpio, GPIO_HIGH);

			if (iport->id == 1)
				ddc_sel_level = chip->pdata->ddc_sel_level;
			else
				ddc_sel_level = !chip->pdata->ddc_sel_level;
			if (chip->pdata->ddc_sel_gpio > 0)
				gpio_direction_output(chip->pdata->ddc_sel_gpio, ddc_sel_level);  

			if (iport->type == SCALER_IN_RGB) {
				if (vga_in_det)
					del_timer_sync(&timer);
				tv5735_init_reg(regs[iport->type].regs, regs[iport->type].len);	
			}
			if (iport->type == SCALER_IN_VGA) {	
				tv5735_init_reg(vga2vga_regs[vga_offset].regs, vga2vga_regs[vga_offset].len);	
				if (vga_in_det)
					add_timer(&timer);
			}
		}else {
			gpio_set_value(iport->led_gpio, GPIO_LOW);
		}
	}
}

static int parse_cmd(unsigned int cmd, unsigned long arg)
{
	printk("tv5735: parse scaler cmd %u\n",cmd);

	switch (cmd) {
		case SCALER_IOCTL_SET_CUR_INPUT:
			set_cur_inport();
			break;
		default:
			break;
	}

	return 0;
}

//enbale chip to process image
static void tv5735_start(void) 
{
	scaler_switch_default_screen();
	set_cur_inport();
	//tv5735_init_reg(regs[chip->cur_in_type].regs, regs[chip->cur_in_type].len);
}

static int tv_hardware_is_ok(struct i2c_client *client)
{
	unsigned char reg_val= 0;
	unsigned  chip_id = 0;

	if (tv_select_bank(client, 0) == 1) {
		reg_val = tv_read_single_data(client, 0x0b);
		chip_id = reg_val;

		msleep(10);
		reg_val = tv_read_single_data(client, 0x0c);
		chip_id = (chip_id << 8) | reg_val;

		msleep(10);
		reg_val = tv_read_single_data(client, 0x0d);
		chip_id = (chip_id << 8) | reg_val;
		printk("%s: Chip Id %#x\n", client->name, chip_id);
		return 0;
	}else {
		return -1;
	}
}

/************pc v\hsync***************/
static void timer_callbak_func(unsigned long arg)
{
	int offset = -0, val1 = -1, val2 = -1;
	int hsync_cnt = 0, vsync_cnt = 0;
	unsigned long vtime, htime;
	struct scaler_platform_data *pdata = (struct scaler_platform_data*)arg;

	vtime = jiffies + msecs_to_jiffies(200);
	val1 = gpio_get_value(pdata->vga_vsync_gpio);
	while (time_after(vtime, jiffies)) {
		val2 = gpio_get_value(pdata->vga_vsync_gpio);
		 if (val1 != val2) {
			 vsync_cnt++;
			 val1 = val2;
		 }
	}
	//printk("vsync cnt = %d\n", vsync_cnt);

	htime = jiffies + msecs_to_jiffies(100);
	val1 = gpio_get_value(pdata->vga_hsync_gpio);
	while (time_after(htime, jiffies)) {
		val2 = gpio_get_value(pdata->vga_hsync_gpio);
		 if (val1 != val2) {
			 hsync_cnt++;
			 val1 = val2;
		 }
	}
	//printk("hsync cnt = %d\n", hsync_cnt);

	//set reg
	offset = vga_offset;
	if (hsync_cnt > 10500 && hsync_cnt < 11500)
		offset = 0;
	if (hsync_cnt > 12000 && hsync_cnt < 12700)
		offset = 1;
	if (hsync_cnt > 9100 && hsync_cnt < 9600)
		offset = 2;


	if (offset != vga_offset) {
		vga_offset = offset;
		tv5735_init_reg(vga2vga_regs[offset].regs, 
				vga2vga_regs[offset].len);
	}

	mod_timer(&timer, (jiffies + msecs_to_jiffies(100)));
}

static void setup_detect_pcsync_timer(const struct scaler_platform_data *pdata)
{
	if (pdata->vga_hsync_gpio > 0) {	
		if (gpio_request(pdata->vga_hsync_gpio, NULL) != 0) {
			printk("%s: request pc hsync detect pin failed\n", __func__);
		}else {
			if (pdata->vga_vsync_gpio > 0) {
				if (gpio_request(pdata->vga_vsync_gpio, NULL) != 0) { 
					gpio_free(pdata->vga_hsync_gpio);
					printk("%s: request pc vsync detect pin failed\n", __func__);
				}else {
					gpio_direction_input(pdata->vga_hsync_gpio);
					gpio_direction_input(pdata->vga_vsync_gpio);
					init_timer(&timer);
					timer.function = timer_callbak_func;
					timer.data = (unsigned long)pdata;
					timer.expires = jiffies + msecs_to_jiffies(3000);
					vga_in_det = 1;
				}//request vsync
			}else {//defined vsync
				gpio_free(pdata->vga_hsync_gpio);
			}
		}//request hsync
	}//defined hsync


}
/********************pc v\hsync************************/

static int tv_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res;
	struct scaler_platform_data  *pdata = client->dev.platform_data;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
	    res = -ENODEV;
		dev_err(&client->adapter->dev, "%s failed %d\n\n", __func__, res);
		return -1;
	}

	if (!pdata) {
		printk("%s: client private data not define\n", __func__);
		return -1;
	}
	
	scaler_init_platform(pdata);

	if (tv_hardware_is_ok(client) != 0) {
		printk("%s chip hardware not ready\n", client->name);
		return -1;
	}

	//alloc chip memory
	chip = alloc_scaler_chip();
	if (!chip) {
		printk("%s: alloc scaler chip memory failed.\n", __func__);
		return -1;
	}else {
		chip->client = client;
		chip->pdata = pdata;
		memcpy((void *)chip->name, (void *)client->name, (strlen(client->name) + 1));
	}

	//init chip
	if (init_scaler_chip(chip, pdata) != 0) {
		goto err;	
	}

	//implement parse cmd function
	chip->parse_cmd = parse_cmd;
	chip->start = tv5735_start;

	//register chip
	if (register_scaler_chip(chip) != 0) {
		printk("%s: register scaler chip failed\n", __func__);
		goto err;
	}
	setup_detect_pcsync_timer(pdata);

	return 0;
err:
	free_scaler_chip(chip);
	return -1;
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
	printk("%s: \n", __func__);

	ret = i2c_add_driver(&tv_i2c_driver);
	if(ret < 0){
		printk("%s, register i2c device, error\n", __func__);
		return ret;
	}

	return 0;
}

static void __exit tv_exit(void)
{
	printk("%s: \n", __func__);
}

module_init(tv_init);
module_exit(tv_exit);

