/*
 * rk1000_tv.c 
 *
 * Driver for rockchip rk1000 tv control
 *  Copyright (C) 2009 
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <asm/uaccess.h>
#include "rk1000_tv.h"

#define DRV_NAME "rk1000_tvout"

//#define DEBUG
#ifdef DEBUG
#define D(fmt, arg...) printk("<7>%s:%d: " fmt, __FILE__, __LINE__, ##arg)
#else
#define D(fmt, arg...)
#endif
#define E(fmt, arg...) printk("<3>!!!%s:%d: " fmt, __FILE__, __LINE__, ##arg)


static volatile int rk1000_tv_output_status = RK28_LCD;

struct i2c_client *rk1000_tv_i2c_client = NULL;


int rk1000_tv_control_set_reg(struct i2c_client *client, u8 reg, u8 const buf[], u8 len)
{
    int ret;
	u8 i2c_buf[8];
	struct i2c_msg msgs[1] = {
		{ client->addr, 0, len + 1, i2c_buf }
	};

	D("reg = 0x%.2X,value = 0x%.2X\n", reg, buf[0]);
	i2c_buf[0] = reg;
	memcpy(&i2c_buf[1], &buf[0], len);
	
	ret = i2c_transfer(client->adapter, msgs, 1);
	if (ret > 0)
		ret = 0;
	
	return ret;
}
#if 0
int rk1000_tv_write_block(u8 addr, u8 *buf, u8 len)
{
	int i;
	int ret = 0;
	
	if(rk1000_tv_i2c_client == NULL){
		printk("<3>rk1000_tv_i2c_client not init!\n");
		return -1;
	}

	for(i=0; i<len; i++){
		ret = rk1000_tv_control_set_reg(rk1000_tv_i2c_client, addr+i, buf+i, 1);
		if(ret != 0){
			printk("<3>rk1000_tv_control_set_reg err, addr=0x%.x, val=0x%.x", addr+i, buf[i]);
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL(rk1000_tv_write_block);

/* drivers/video/rk28_fb.c */
extern int win0fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg);
extern int get_lcd_width(void);
extern int get_lcd_height(void);

/* drivers/video/rk28_backlight.c */
extern void rk28_bl_suspend(void *);
extern void rk28_bl_resume(void *);

int rk28_tvout_win0fb_set_par(int x, int y, int w, int h)
{
	int ret;
	struct fb_info *info = registered_fb[1]; // fb1
	struct fb_var_screeninfo var;

	var = info->var;
	var.nonstd = ((y << 20) & 0xfff00000) + 
	             ((x << 8 )& 0xfff00) + 3;	    //win0 ypos & xpos & format (ypos<<20 + xpos<<8 + format)    // 2
	var.grayscale = ((h << 20) & 0xfff00000) +
	                ((w << 8) & 0xfff00) + 0;   //win0 xsize & ysize;

	acquire_console_sem();
	info->flags |= FBINFO_MISC_USEREVENT;
	ret = fb_set_var(info, &var);
	info->flags &= ~FBINFO_MISC_USEREVENT;
	release_console_sem();

	if(ret < 0){
		E("fb_set_var err!\n");
	}

	return ret;
}


int rk1000_tv_set_output(int type)
{
	struct fb_info *info = registered_fb[1]; // fb1
	int ret = 0;

	D("type = %d\n", type);
	
	switch(type){
	case RK28_LCD:
		if(rk1000_tv_output_status == RK28_LCD){
			break;
		}
		rk1000_tv_output_status = RK28_LCD;
		win0fb_ioctl(info, 0x5001, 0);
		rk28_tvout_win0fb_set_par(0, 0, get_lcd_width(), get_lcd_height());
		rk28_bl_resume((void *)0);
		break;
	case Cvbs_NTSC:
		if(rk1000_tv_output_status == Cvbs_NTSC){
			break;
		}
		rk28_bl_suspend((void *)0);
		rk1000_tv_output_status = Cvbs_NTSC;
		win0fb_ioctl(info, 0x5001, 1);
		rk28_tvout_win0fb_set_par(0, 0, 720, 480);
		break;
	case Cvbs_PAL:
		if(rk1000_tv_output_status == Cvbs_PAL){
			break;
		}
		rk28_bl_suspend((void *)0);
		rk1000_tv_output_status = Cvbs_PAL;
		win0fb_ioctl(info, 0x5001, 2);
		rk28_tvout_win0fb_set_par(0, 0, 720, 576);
		break;
	case Ypbpr480:
		if(rk1000_tv_output_status == Ypbpr480){
			break;
		}
		rk28_bl_suspend((void *)0);
		rk1000_tv_output_status = Ypbpr480;
		win0fb_ioctl(info, 0x5001, 3);
		rk28_tvout_win0fb_set_par(0, 0, 720, 480);
		break;
	case Ypbpr576:
		if(rk1000_tv_output_status == Ypbpr576){
			break;
		}
		rk28_bl_suspend((void *)0);
		rk1000_tv_output_status = Ypbpr576;
		win0fb_ioctl(info, 0x5001, 4);
		rk28_tvout_win0fb_set_par(0, 0, 720, 576);
		break;
	case Ypbpr720:
		if(rk1000_tv_output_status == Ypbpr720){
			break;
		}
		rk28_bl_suspend((void *)0);
		rk1000_tv_output_status = Ypbpr720;
		win0fb_ioctl(info, 0x5001, 5);
		rk28_tvout_win0fb_set_par(0, 0, 1280, 720);
		break;
	default:
		ret = -EINVAL;
		E("Invalid type, type = %d\n", type);
		break;
	}

	return ret;
}

int rk1000_tv_get_output_status(void)
{
	return rk1000_tv_output_status;
}

int rk1000_tv_open(struct inode *inode, struct file *filp)
{
	return 0;
}

ssize_t rk1000_tv_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp)
{
	char c = 0;
	int ret;
	
	ret = copy_from_user(&c, buff, 1);
	if(ret < 0){
		E("copy_from_user err!\n");
		return ret;
	}

	rk1000_tv_set_output(c - '0');

	return 1;
}

int rk1000_tv_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	
	switch (cmd){
	case RK1000_TV_SET_OUTPUT:
		ret = rk1000_tv_set_output(arg);
		break;
	default:
		E("unknown ioctl cmd!\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

int rk1000_tv_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static struct file_operations rk1000_tv_fops = {
	.owner   = THIS_MODULE,
	.open    = rk1000_tv_open,
	.write   = rk1000_tv_write,
	.ioctl   = rk1000_tv_ioctl,
	.release = rk1000_tv_release,
};

struct miscdevice rk1000_tv_misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRV_NAME,
	.fops = &rk1000_tv_fops,
};
#endif
static int rk1000_tv_control_remove(struct i2c_client *client)
{
	return 0;
}

static int rk1000_tv_control_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int rc = 0;
	u8 buff;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENODEV;
		goto failout;
	}

	rk1000_tv_i2c_client = client;
	
    //standby tv 
    buff = 0x07;  
    rk1000_tv_control_set_reg(client, 0x03, &buff, 1);
    printk("rk1000_tv_control probe ok\n");
	return 0;

failout:
	kfree(client);
	return rc;
}

static const struct i2c_device_id rk1000_tv_control_id[] = {
	{ DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk1000_control_id);

static struct i2c_driver rk1000_tv_control_driver = {
	.driver 	= {
		.name	= DRV_NAME,
	},
	.id_table = rk1000_tv_control_id,
	.probe = rk1000_tv_control_probe,
	.remove = rk1000_tv_control_remove,
};

static int __init rk1000_tv_init(void)
{    
    int ret;
    ret = i2c_add_driver(&rk1000_tv_control_driver);
	if(ret < 0){
		E("i2c_add_driver err, ret = %d\n", ret);
		goto err1;
	}

	//ret = misc_register(&rk1000_tv_misc_dev);
	//if(ret < 0){
	//	E("misc_register err, ret = %d\n", ret);
	//	goto err2;
	//}
	
    return 0;
	
err2:
	i2c_del_driver(&rk1000_tv_control_driver);
err1:
	return ret;
}

static void __exit rk1000_tv_exit(void)
{
	//misc_deregister(&rk1000_tv_misc_dev);
    i2c_del_driver(&rk1000_tv_control_driver);
}

module_init(rk1000_tv_init);
module_exit(rk1000_tv_exit);
/* Module information */
MODULE_DESCRIPTION("ROCKCHIP rk1000 tv ");
MODULE_LICENSE("GPL");

