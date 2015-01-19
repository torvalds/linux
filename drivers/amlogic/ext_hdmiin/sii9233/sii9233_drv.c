/*
 * HDMI Receiver SiI9233A Driver
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <mach/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c-aml.h>
#include <linux/amlogic/aml_gpio_consumer.h>

#include "sii9233_drv.h"
#include "vdin_interface.h"
#include "sii9233_interface.h"


extern int sii9233a_register_tvin_frontend(struct tvin_frontend_s *frontend);

#define SII9233A_I2C_ADDR	0x60

static sii9233a_info_t sii9233a_info;
static struct task_struct *sii9233a_task;
//static struct timer_list hdmirx_timer;
static unsigned int sii_output_mode = 0xff;

#define HDMIRX_PRINT_FUNC()  printk("HDMIRX_SiI9233A FUNC: %s [%d]\n", __FUNCTION__, __LINE__)

#ifdef DEBUG
#define pr_dbg(fmt, args...) printk(KERN_DEBUG "aml_sii9233a: " fmt, ## args)
#else
#define pr_dbg(fmt, args...)
#endif
#define pr_error(fmt, args...) printk(KERN_ERR "aml_sii9233a: " fmt, ## args)

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a i2c interface
int aml_sii9233a_i2c_read(unsigned char dev_addr, char *buf, int addr_len, int data_len)
{
	int ret;
	char reg_addr = buf[0];

	struct i2c_msg msg[] =
	{
		{
			.addr	= (dev_addr >> 1),
			.flags	= 0,
			.len	= addr_len,
			.buf	= buf,
		},
		{
			.addr	= (dev_addr >> 1),
			.flags	= I2C_M_RD,
			.len	= data_len,
			.buf	= buf,
		},
	};

	if( sii9233a_info.i2c_client == NULL )
	{
		printk("[%s] invalid i2c_client !\n", __FUNCTION__);
		return -1;
	}

	ret = i2c_transfer(sii9233a_info.i2c_client->adapter, msg, 2);
	if( ret < 0 )
		pr_err("hdmirx i2c read error: %d, dev = 0x%x, reg = 0x%x\n",ret,dev_addr,reg_addr);

	return ret;
}

int aml_sii9233a_i2c_write(unsigned char dev_addr, char *buf, int len)
{
	int ret = -1;
	char reg_addr = buf[0], value = buf[1];
	struct i2c_msg msg[] =
	{
		{
			.addr	= (dev_addr >> 1),
			.flags	= 0,
			.len	= len,
			.buf	= buf,
		},
	};


	if( sii9233a_info.i2c_client == NULL )
	{
		printk("[%s] invalid i2c_client !\n", __FUNCTION__);
		return -1;
	}

	ret = i2c_transfer(sii9233a_info.i2c_client->adapter, msg, 1);
	if( ret < 0 )
		pr_err("hdmirx i2c write error: %d, dev = 0x%x, reg = 0x%x, value = 0x%x!\n",ret,dev_addr,reg_addr,value);
	
	return ret;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
unsigned int sii9233a_get_output_mode(void)
{
	unsigned int h_active,h_total,v_active,v_total;
	unsigned int mode = 0;

	h_active = sii_get_h_active();
	h_total = sii_get_h_total();
	v_active = sii_get_v_active();
	v_total = sii_get_v_total();

	printk("[%s]: pixel = %d x %d ( %d x %d )\n", __FUNCTION__, h_active, v_active, h_total, v_total);

	if( (h_total==2200) && (v_active==1080) )			mode = CEA_1080P60;// 1080p
	else if( (h_total==2640) && (v_active==1080) )		mode = CEA_1080P50;// 1080p50
	else if( (h_total==2200) && (v_active==540) )		mode = CEA_1080I60;// 1080i
	else if( (h_total==2640) && (v_active==540) )		mode = CEA_1080I50;// 1080i50
	else if( (h_total==1650) && (v_active==720) )		mode = CEA_720P60;// 720p
	else if( (h_total==1980) && (v_active==720) )		mode = CEA_720P50;// 720p50
	else if( (h_total==864) && (v_active==576) )		mode = CEA_576P50;// 576p
	else if( (h_total==858) && (v_active==480) )		mode = CEA_480P60;// 480p
	else if( (h_total==864) && (v_active==288) )		mode = CEA_576I50;// 576i
	else if( (h_total==858) && (v_active==240) )		mode = CEA_480I60;// 480i

	return mode;
}

static void sii9233a_start_vdin_mode(unsigned int mode)
{
	unsigned int height = 0, width = 0, frame_rate = 0, field_flag = 0;

	printk("[%s], start with mode = %d\n", __FUNCTION__, mode);
	switch(mode)
	{
		case CEA_480I60:
			width = 1440;	height = 480;	frame_rate = 60;	field_flag = 1;		
			break;
		
		case CEA_480P60:
			width = 720;	height = 480;	frame_rate = 60;	field_flag = 0;		
			break;
		
		case CEA_576I50:
			width = 1440;	height = 576;	frame_rate = 50;	field_flag = 1;		
			break;
		
		case CEA_576P50:
			width = 720;	height = 576;	frame_rate = 50;	field_flag = 0;		
			break;
		
		case CEA_720P50:
			width = 1280;	height = 720;	frame_rate = 50;	field_flag = 0;		
			break;
		
		case CEA_720P60:
			width = 1280;	height = 720;	frame_rate = 60;	field_flag = 0;		
			break;
		
		case CEA_1080I60:
			width = 1920;	height = 1080;	frame_rate = 60;	field_flag = 1;		
			break;
		
		case CEA_1080P60:
			width = 1920;	height = 1080;	frame_rate = 60;	field_flag = 0;		
			break;
		
		case CEA_1080I50:
			width = 1920;	height = 1080;	frame_rate = 50;	field_flag = 1;		
			break;
		
		case CEA_1080P50:
			width = 1920;	height = 1080;	frame_rate = 50;	field_flag = 0;		
			break;
		
		default:
			printk("[%s], invalid video mode!\n",__FUNCTION__);
			return ;
	}

	sii9233a_start_vdin(&sii9233a_info,width,height,frame_rate,field_flag);

	return ;
}

void sii9233a_output_mode_trigger(unsigned int flag)
{
	unsigned int mode = 0xff;

	sii9233a_info.signal_status = flag;
	printk("[%s] set signal_status = %d\n", __FUNCTION__, sii9233a_info.signal_status);

	if ( (sii9233a_info.user_cmd == 0) || (sii9233a_info.user_cmd == 0x4) || (sii9233a_info.user_cmd == 0xff) )
		return;

	if( (0 == flag) && ((sii9233a_info.user_cmd == 1) || (sii9233a_info.user_cmd == 3)) )
	{
		printk("[%s], lost signal, stop vdin!\n", __FUNCTION__);
		sii_output_mode = 0xff;
		sii9233a_stop_vdin(&sii9233a_info);
		return ;
	}

	if( (1 == flag) && ((sii9233a_info.user_cmd == 2) || (sii9233a_info.user_cmd == 3)) )
	{
		msleep(500);
		mode = sii9233a_get_output_mode();
		if( mode != sii_output_mode )
		{
			printk("[%s], trigger new mode = %d, old mode = %d\n", __FUNCTION__, mode, sii_output_mode);
			if (mode < CEA_MAX)
			{
				sii9233a_start_vdin_mode(mode);
				sii_output_mode = mode;
			}
		}
	}

	return ;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static ssize_t sii9233a_debug_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "command format:\n\t\tr device_address reg_address\
												\n\t\trb device_address reg_address length\
												\n\t\tdump device_address reg_start reg_end\
												\n\t\tw device_address reg_address value !");
}

static ssize_t sii9233a_debug_store(struct class *class, struct class_attribute *attr,
									const char *buf, size_t count)
{
	int argn;
	char *p=NULL, *para=NULL, *argv[4] = {NULL,NULL,NULL,NULL};
	unsigned char cmd=0, dev_addr = 0, reg_start = 0, reg_end = 0, length = 0, value = 0xff;
	char i2c_buf[2] = {0,0};
	int ret = 0;

	p = kstrdup(buf, GFP_KERNEL);
	for( argn=0; argn<4; argn++ )
	{
		para = strsep(&p, " ");
		if( para == NULL )
			break;
		argv[argn] = para;
	}

//	printk("get para: %s %s %s %s!\n",argv[0],argv[1],argv[2],argv[3]);

	if( !strcmp(argv[0], "r") )
		cmd = 0;
	else if( !strcmp(argv[0], "rb") )
		cmd = 1; 
	else if( !strcmp(argv[0], "dump") )
		cmd = 2;
	else if( !strcmp(argv[0], "w") )
		cmd = 3;
	else if( !strcmp(argv[0], "vinfo\n") )
		cmd = 4;
	else if( !strncmp(argv[0], "tt", strlen("tt")) )
		cmd = 5;
	else
	{
		printk("invalid cmd = %s\n", argv[0]);
		return count;
	}

	printk(" cmd = %d - \"%s\"\n", cmd, argv[0]);
	if( (argn<1) || ((cmd==0)&&argn!=3) || ((cmd==1)&&(argn!=4)) || ((cmd==2)&&(argn!=4)) )
	{
		printk("invalid command format!\n");
		kfree(p);
		return count;
	}

	if( cmd < 4 )
		dev_addr = (unsigned char)simple_strtoul(argv[1],NULL,16);

	if( cmd == 0 ) // read
	{
		reg_start = (unsigned char)simple_strtoul(argv[2],NULL,16);
		printk("\nsii9233a debug read dev = 0x%x, reg = 0x%x\n",dev_addr, reg_start);
		i2c_buf[0] = reg_start;

		ret = aml_sii9233a_i2c_read(dev_addr, i2c_buf, 1, 1);
		printk("sii9233a i2c read ret = %d, value = 0x%x\n",ret, i2c_buf[0]);

	}
	else if( cmd == 1 ) // read block
	{
		char *tmp = NULL;
		int i = 0;

		reg_start = (unsigned char)simple_strtoul(argv[2],NULL,16);
		length = (unsigned char)simple_strtoul(argv[3],NULL,16);
		printk("\nsii9233a debug read block dev = 0x%x, start = 0x%x, length = 0x%x\n",dev_addr, reg_start, length);

		tmp = (char*)kmalloc(length, GFP_KERNEL);
		if( tmp != NULL )
		{
			tmp[0] = reg_start;
			ret = aml_sii9233a_i2c_read(dev_addr, tmp, 1, length);

			for( i=0; i<length; i++ )
			{
				if( i%0x10 == 0 )
					printk("%.2X:", i);
				printk(" %.2X",tmp[i]);
				if( (i+1)%0x10 == 0 )
					printk("\n");
			}
			printk("\n");
			kfree(tmp);
		}
	}
	else if( cmd == 2 ) // dump
	{
		int i = 0;

		reg_start = (unsigned char)simple_strtoul(argv[2],NULL,16);
		reg_end = (unsigned char)simple_strtoul(argv[3],NULL,16);
		printk("\nsii9233a debug dump dev = 0x%x, start = 0x%x, end = 0x%x\n",dev_addr, reg_start, reg_end);
		
		for( i=reg_start; i<=reg_end; i++ )
		{
			i2c_buf[0] = i;
			ret = aml_sii9233a_i2c_read(dev_addr, i2c_buf, 1, 1);
			if( i%0x10 == 0 )
					printk("%.2X:", i);
			printk(" %.2X",i2c_buf[0]);
			if( (i+1)%0x10 == 0 )
				printk("\n");
		}
		printk("\n");
	}
	else if( cmd == 3 ) // write
	{
		reg_start = (unsigned char)simple_strtoul(argv[2],NULL,16);
		value = (unsigned char)simple_strtoul(argv[3],NULL,16); 
		printk("\nsii9233a debug write dev = 0x%x, reg = 0x%x, value = 0x%x\n",dev_addr, reg_start, value);
		i2c_buf[0] = reg_start;
		i2c_buf[1] = value;

		ret = aml_sii9233a_i2c_write(dev_addr, i2c_buf, 2);
		printk("sii9233a i2c write ret = %d\n",ret);
		ret = aml_sii9233a_i2c_read(dev_addr, i2c_buf, 1, 1);
		printk("sii9233a i2c read back ret = %d, value = 0x%x\n",ret, i2c_buf[0]);
	}
	else if( cmd == 4 ) // get hdmi input video info
	{
		printk("begin dump hdmi-in video info:\n");
		dump_input_video_info();
	}
	else if( cmd == 5 ) // tt, for loop test of 9293 i2c
	{
		unsigned int type = 255, count = 0;

		type = (unsigned int )simple_strtoul(argv[1], NULL, 10);
		count = (unsigned int)simple_strtoul(argv[2], NULL, 10);

		printk("9233 i2c stability test: type = %d, count = %d\n", type, count);

		if( type == 0 ) // 0x2/0x3 = 3392
		{
			unsigned int i = 0, v1 = 0, v2 = 0;
			unsigned int err1 = 0, err2 = 0;
			for( i=0; i<count; i++ )
			{
				msleep(2);
				v1 = sii_get_chip_id();
				msleep(2);
				v2 = sii_get_h_total();
				
				if( v1 != 0x9233 )
				{
					err1 ++;
					printk("sii2ctest, ID failed: [%d], [%d] = 0x%x\n", i, err1, v1);
				}
				if( v2 != 0x672 )
				{
					err2 ++;
					printk("sii2ctest, RES_H failed: [%d], [%d] = 0x%x\n", i, err2, v2);
				}

				printk("sii2ctest, [%d]: err1 = %d, v1 = 0x%x, err2 = %d v2 = 0x%x\n", i, err1, v1, err2, v2);
			}
		}
	}

	kfree(p);
	return count;
}
#if 0
static ssize_t sii9233a_reset_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return 0;
}
static ssize_t sii9233a_reset_store(struct class *class, struct class_attribute *attr,
									const char *buf, size_t count)
{
	unsigned char value = 0;
    char *endp;

    value = (unsigned char)simple_strtoul(buf, &endp, 0);

    if( value != 0 )
    {
    	sii_hardware_reset(&sii9233a_info);
	}
    
    return count;
}

#endif
static ssize_t sii9233a_port_show(struct class *class, struct class_attribute *attr, char *buf)
{
	char port = 0xff;

	port = sii_get_hdmi_port();

	return sprintf(buf, "current sii9233a input port = %d\n", port);
}

static ssize_t sii9233a_port_store(struct class *class, struct class_attribute *attr,
									const char *buf, size_t count)
{
	unsigned char port = 0;
	char *endp;

	port = (unsigned char)simple_strtoul(buf, &endp, 0);

	if( port < 4 )
	{
		sii_set_hdmi_port(port);
		printk("set sii9233a input port = %d\n",port);
	}
	return count;
}
static ssize_t sii9233a_enable_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "sii9233a tvin enable = %d\n", sii9233a_info.user_cmd);
}
static ssize_t sii9233a_enable_store(struct class *class, struct class_attribute *attr,
									const char *buf, size_t count)
{
	int argn;
	char *p=NULL, *para=NULL, *argv[5] = {NULL,NULL,NULL,NULL,NULL};
	unsigned int mode = 0, enable=0;
	char *vmode[10] = {"480i\n","480p\n","576i\n","576p\n","720p50\n","720p\n","1080i\n","1080p\n","1080i50\n","1080p50\n"};
	int i = 0;

	p = kstrdup(buf, GFP_KERNEL);
	for( argn=0; argn<4; argn++ )
	{
		para = strsep(&p, " ");
		if( para == NULL )
			break;
		argv[argn] = para;
	}

//	printk("argn = %d, \"%s\", \"%s\", \"%s\", \"%s\", \"%s\"\n", argn, argv[0], argv[1], argv[2], argv[3], argv[4] );
	if( !strcmp(argv[0], "0\n") ) // disable
		enable = 0;
	else if( !strcmp(argv[0], "1\n") ) // enable, driver will trigger to vdin-stop
		enable = 1;
	else if( !strcmp(argv[0], "2\n") ) // enable, driver will trigger to vdin-start
		enable = 2;
	else if( !strcmp(argv[0], "3\n") ) // enable, driver will trigger to vdin-start/vdin-stop
		enable = 3;
	else if( !strcmp(argv[0], "4\n") ) // enable, driver will not trigger to vdin-start/vdin-stop
		enable = 4;
	else
	{
		for( i=0; i<10; i++ )
		{
			if( !strcmp(argv[0], vmode[i]) )
			{
				mode = i;
				enable = 0xff;
			}
		}
	}

	sii9233a_info.user_cmd = enable;

	if( (enable==1) && (argn!=5) && (argn!=1) )
	{
		printk("invalid parameters to enable cmd !\n");
		return count;
	}

	if( (enable==0) && (sii9233a_info.vdin_started==1) )
	{
		sii9233a_stop_vdin(&sii9233a_info);
		printk("sii9233a disable dvin !\n");
	}
	else if( ( (enable==1)||(enable==2)||(enable==3)||(enable==4) ) && (sii9233a_info.vdin_started==0) )
	{
		mode = sii9233a_get_output_mode();
		sii9233a_start_vdin_mode(mode);
		printk("sii9233a enable(0x%x) dvin !\n", enable);
	}
	else if( (enable==0xff) && (sii9233a_info.vdin_started==0) )
	{
		
		switch(mode)
		{
			case 0: // 480i
				mode = CEA_480I60;		break;
			case 1: // 480p
				mode = CEA_480P60;		break;
			case 2: // 576i
				mode = CEA_576I50;		break;
			case 3: // 576p
				mode = CEA_576P50;		break;
			case 4: // 720p50
				mode = CEA_720P50;		break;
			case 5: // 720p60
			default:
				mode = CEA_720P60;		break;
			case 6: // 1080i60
				mode = CEA_1080I60;		break;
			case 7: // 1080p60
				mode = CEA_1080P60;		break;
			case 8: // 1080i50
				mode = CEA_1080I50;		break;
			case 9: // 1080p50
				mode = CEA_1080P50;		break;
		}

		sii9233a_start_vdin_mode(mode);
		printk("sii9233a enable(0x%x) dvin !\n", enable);
	}

	return count;
}

static ssize_t sii9233a_input_mode_show(struct class *class, struct class_attribute *attr, char *buf)
{
	unsigned int mode = 0xff;
	char hdmi_mode_str[16], mode_str[16] ;
	unsigned char value;

	value = sii_is_hdmi_mode();
	memset(hdmi_mode_str, 0x00, 16);
	memset(mode_str, 0x00, 8);

	strcpy(hdmi_mode_str,(value==0)?"DVI:":"HDMI:");
	mode = sii9233a_get_output_mode();

	switch(mode)
	{
		case CEA_480I60:	strcpy(mode_str, "480i");		break;
		case CEA_480P60:	strcpy(mode_str, "480p");		break;
		case CEA_576I50:	strcpy(mode_str, "576i");		break;
		case CEA_576P50:	strcpy(mode_str, "576p");		break;
		case CEA_720P60:	strcpy(mode_str, "720p");		break;
		case CEA_720P50:	strcpy(mode_str, "720p50hz");	break;
		case CEA_1080I60:	strcpy(mode_str, "1080i");		break;
		case CEA_1080I50:	strcpy(mode_str, "1080i50hz");	break;
		case CEA_1080P60:	strcpy(mode_str, "1080p");		break;
		case CEA_1080P50:	strcpy(mode_str, "1080p50hz");	break;
		default:			strcpy(mode_str, "invalid");	break;
	}

	if( strcmp(mode_str, "invalid") != 0 )
		strcat(hdmi_mode_str, mode_str);
	else
		strcpy(hdmi_mode_str, mode_str);
	return sprintf(buf, "%s\n", hdmi_mode_str);
}

static ssize_t sii9233a_cable_status_show(struct class *class, struct class_attribute *attr, char *buf)
{
	sii9233a_info.cable_status = sii_get_pwr5v_status();
	return sprintf(buf, "%d\n", sii9233a_info.cable_status);
}

static ssize_t sii9233a_signal_status_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", sii9233a_info.signal_status);
}

static ssize_t sii9233a_audio_sr_show(struct class *class, struct class_attribute *attr, char *buf)
{
	int audio_sr;
	char *audio_sr_array[] =
	{
		"44.1 kHz",			// 0x0
		"Not indicated",	// 0x1
		"48 kHz",			// 0x2
		"32 kHz",			// 0x3
		"22.05 kHz",		// 0x4
		"reserved",			// 0x5
		"24 kHz",			// 0x6
		"reserved",			// 0x7
		"88.2 kHz",			// 0x8
		"768 kHz (192*4)",	// 0x9
		"96 kHz",			// 0xa
		"reserved",			// 0xb
		"176.4 kHz",		// 0xc
		"reserved",			// 0xd
		"192 kHz",			// 0xe
		"reserved"			// 0xf
	};

	audio_sr = sii_get_audio_sampling_freq()&0xf;

	return sprintf(buf, "%s\n", audio_sr_array[audio_sr]);
}

static struct class_attribute sii9233a_class_attrs[] =
{
	__ATTR(debug,			S_IRUGO|S_IWUSR,	sii9233a_debug_show,			sii9233a_debug_store),
//	__ATTR(reset,			S_IRUGO|S_IWUSR,	sii9233a_reset_show,			sii9233a_reset_store),
	__ATTR(port,			S_IRUGO|S_IWUSR,	sii9233a_port_show,				sii9233a_port_store),
	__ATTR(enable,			S_IRUGO|S_IWUSR,	sii9233a_enable_show,			sii9233a_enable_store),
	__ATTR(input_mode,		S_IRUGO,			sii9233a_input_mode_show,		NULL),
	__ATTR(cable_status,	S_IRUGO,			sii9233a_cable_status_show,	NULL),
	__ATTR(signal_status,	S_IRUGO,			sii9233a_signal_status_show,	NULL),
	__ATTR(audio_sample_rate,	S_IRUGO,			sii9233a_audio_sr_show,	NULL),
	__ATTR_NULL	
};

static struct class sii9233a_class =
{
	.name = SII9233A_DRV_NAME,
	.class_attrs = sii9233a_class_attrs,	
};

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#if 0
static void hdmirx_timer_handler(unsigned long arg)
{
    mod_timer(&hdmirx_timer, jiffies + HZ);
}

static void hdmirx_timer_init(void)
{
	init_timer(&hdmirx_timer);
	hdmirx_timer.data = (ulong)0;
	hdmirx_timer.function = hdmirx_timer_handler;
	hdmirx_timer.expires = jiffies + HZ;
	add_timer(&hdmirx_timer);
}
#endif

extern void TIMER_Init(void);
extern void sii9223a_main(void);

static int sii9233a_task_handler(void *data)
{
	TIMER_Init();
	
	sii_hardware_reset(&sii9233a_info);

	//sii_set_hdmi_port(3);

	while(1)
	{
		sii9223a_main();
		msleep(100);
	}

	return 0;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifdef CONFIG_USE_OF
static int sii9233a_get_of_data(struct device_node *pnode)
{
	struct device_node *hdmirx_node = pnode;
	struct i2c_board_info board_info;
	struct i2c_adapter *adapter;
	unsigned int i2c_index;
	
	const char *str;
	int ret = 0;

	memset(&sii9233a_info, 0x0, sizeof(sii9233a_info_t));

// for i2c bus
	ret = of_property_read_string(hdmirx_node, "i2c_bus", &str);
	if (ret) {
		printk("[%s]: faild to get i2c_bus str!\n", __FUNCTION__);
		return -1;
	} else {
		if (!strncmp(str, "i2c_bus_ao", 9))
			i2c_index = AML_I2C_MASTER_AO;
		else if (!strncmp(str, "i2c_bus_a", 9))
			i2c_index = AML_I2C_MASTER_A;
		else if (!strncmp(str, "i2c_bus_b", 9))
			i2c_index = AML_I2C_MASTER_B;
		else if (!strncmp(str, "i2c_bus_c", 9))
			i2c_index = AML_I2C_MASTER_C;
		else if (!strncmp(str, "i2c_bus_d", 9))
			i2c_index = AML_I2C_MASTER_D;
		else
			return -1; 
	}
	
	sii9233a_info.i2c_bus_index = i2c_index;

// for gpio_reset
	ret = of_property_read_string(hdmirx_node, "gpio_reset", &str);
	if (ret) {
		printk("[%s]: faild to get gpio_rst!\n", __FUNCTION__);
		return -2;
	}

	ret = amlogic_gpio_name_map_num(str);
	if (ret < 0)
	{
		printk("[%s]: faild to map gpio_rst !\n", __FUNCTION__);
	}
	else
		sii9233a_info.gpio_reset = ret;

// for irq
#if 0
	ret = of_property_read_string(hdmirx_node,"gpio_intr",&str);
	if(ret)
	{
		printk("[%s]: failed to get INT!\n", __FUNCTION__);
		return -3;
	}

	ret = amlogic_gpio_name_map_num(str);
	if(ret < 0 )
	{
		printk("[%s]: failed to map gpio_intr!\n", __FUNCTION__);
	}
	else
		sii9233a_info.gpio_intr = ret;
#endif

	memset(&board_info, 0x0, sizeof(board_info));
	strncpy(board_info.type, SII9233A_DRV_NAME, I2C_NAME_SIZE);
	adapter = i2c_get_adapter(i2c_index);
	board_info.addr = SII9233A_I2C_ADDR;
	board_info.platform_data = &sii9233a_info;

	sii9233a_info.i2c_client = i2c_new_device(adapter, &board_info);

	return 0;
}
#endif

static int sii9233a_probe(struct platform_device *pdev)
{
	int ret = 0;
	
#ifdef CONFIG_USE_OF
	sii9233a_get_of_data(pdev->dev.of_node);
#else
	hdmirx_pdata = pdev->dev.platform_data;
	if( !hdmirx_pdata )
	{
		printk("[%s] failed to get platform data !\n", __FUNCTION__);
		return -ENOENT;
	}
#endif

	amlogic_gpio_request(sii9233a_info.gpio_reset, SII9233A_DRV_NAME);

	ret = sii9233a_register_tvin_frontend(&(sii9233a_info.tvin_frontend));
	if( ret < 0 )
	{
		printk("[%s] register tvin frontend failed ret = %d!\n", __FUNCTION__, ret);
	}

	sii9233a_task = kthread_run(sii9233a_task_handler, &sii9233a_info, "kthread_sii9233a");

	ret = class_register(&sii9233a_class);
	if (ret)
	{
		printk("[%s] failed to register class ret = %d!\n", __FUNCTION__, ret);
	}

	return ret;
}

static int sii9233a_remove(struct platform_device *pdev)
{
	return 0;	
}

#ifdef CONFIG_USE_OF
static const struct of_device_id sii9233a_dt_match[] = {
	{
		.compatible			= "amlogic,sii9233",
	},
	{},
};
#else
#define sii9223a_dt_match	NULL
#endif


static struct platform_driver sii9233a_driver = {
	.probe			= sii9233a_probe,
	.remove			= sii9233a_remove,
	.driver			= {
						.name				= SII9233A_DRV_NAME,
						.owner				= THIS_MODULE,
						.of_match_table		= sii9233a_dt_match,
					}	
};
static int __init sii9233a_drv_init(void)
{
//	int ret = 0;

    printk("[%s] Ver: %s\n", __FUNCTION__, SII9233A_DRV_VER);

    if (platform_driver_register(&sii9233a_driver))
    {
        printk("[%s] failed to register drv!\n", __FUNCTION__);
        return -ENODEV;
    }

    return 0;
}

static void __exit sii9233a_drv_exit(void)
{
    printk("[%s]\n", __FUNCTION__);
    platform_driver_unregister(&sii9233a_driver);
    class_unregister(&sii9233a_class);
    
    return ;
}

module_init(sii9233a_drv_init);
module_exit(sii9233a_drv_exit);

MODULE_DESCRIPTION("AML SiI9233A driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
