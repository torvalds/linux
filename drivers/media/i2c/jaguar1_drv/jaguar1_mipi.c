// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
 *
 *  Copyright (C) 2016 	NEXTCHIP Inc. All rights reserved.
 *  Module		: Jaguar1 Device Driver
 *  Description	: MIPI
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#include <linux/string.h>
#include <linux/delay.h>
#include "jaguar1_common.h"
#include "jaguar1_mipi.h"
#include "jaguar1_mipi_table.h"

static unsigned char mipi_dtype, arb_dtype, en_param;


/*-------------------------------------------------------------------

  Arbiter function 

  -------------------------------------------------------------------*/

static void arb_scale_set(video_input_init *dev_ch_info, unsigned char val)
{
	int devnum = dev_ch_info->ch / 4;
	unsigned char arb_scale = 0;

	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x20);

	arb_scale = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x01);
	arb_scale &= ~(0x3<<(dev_ch_info->ch*2));
	arb_scale |= val<<(dev_ch_info->ch*2);

	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x01, arb_scale);
}

void arb_enable(int dev_num)
{
	if((dev_num < 0) || (dev_num > 3))
	{
		printk("[DRV] %s input channel Error (%d)\n",__func__, dev_num);
		return;
	}

	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xff, 0x20);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x00, en_param);
	printk("VDEC_ARBITER_INIT done 0x%X\n", en_param);
}

void arb_disable(int dev_num)
{
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xff, 0x20);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x00, 0x00);
}

void arb_init(int dev_num)
{
	arb_disable(dev_num);

	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xff, 0x20);

	// ARB RESET High
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x40, 0x01);
	// MIPI Video type Init
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x0F, arb_dtype);
	// ARB 32Bit Mode
	if(2 == jaguar1_lane)
		gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x0D, 0x00); 
	else
		gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x0D, 0x01);

	// ARB RESET Low
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x40, 0x00);

	arb_enable(dev_num);
}


/*-------------------------------------------------------------------

  MIPI function 

-------------------------------------------------------------------*/

static void mipi_frame_opt_set(video_input_init *dev_ch_info, unsigned char val)
{
	int devnum = dev_ch_info->ch / 4;
	unsigned char mipi_frame_opt;

	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x21);

	switch(dev_ch_info->ch)
	{
		case 0 :
			mipi_frame_opt = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x3E);
			mipi_frame_opt = (mipi_frame_opt & 0xF0) | val;
			gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x3E, mipi_frame_opt);
			break;
		case 1 :
			mipi_frame_opt = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x3E);
			mipi_frame_opt = (mipi_frame_opt & 0x0F) | val;
			gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x3E, mipi_frame_opt);
			break;
		case 2 :
			mipi_frame_opt = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x3F);
			mipi_frame_opt = (mipi_frame_opt & 0xF0) | val;
			gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x3F, mipi_frame_opt);
			break;
		case 3 :
			mipi_frame_opt = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x3F);
			mipi_frame_opt = (mipi_frame_opt & 0x0F) | val;
			gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x3F, mipi_frame_opt);
			break;
	}
}

void mipi_video_format_set(video_input_init *dev_ch_info)
{
	mipi_vdfmt_set_s mipi_vd_fmt = (mipi_vdfmt_set_s)decoder_mipi_fmtdef[dev_ch_info->format];

	if(dev_ch_info->interface != DISABLE)
	{
		en_param |= 0x11<<(dev_ch_info->ch);
	}

	mipi_frame_opt_set(dev_ch_info, mipi_vd_fmt.mipi_frame_opt);
	arb_scale_set(dev_ch_info, mipi_vd_fmt.arb_scale);
}

int mipi_datatype_set(unsigned char data_type)
{
	int ret = 0;

	switch(data_type)
	{
		case VD_DATA_TYPE_YUV422 :
			mipi_dtype = 0x1E;
			arb_dtype = 0x00;
			break;
		case VD_DATA_TYPE_YUV420 :
			mipi_dtype = 0x18;
			arb_dtype = 0xAA;
			break;
		case VD_DATA_TYPE_LEGACY420 :
			mipi_dtype = 0x1A;
			arb_dtype = 0x55;
			break;
		default :
			printk("[DRV]%s : invalid data type [0x%X]\n", __func__,  data_type);
			ret = -1;
			break;
	}

	return ret;
}

void mipi_tx_init(int dev_num)
{
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xFF, 0x21);	

	pr_info("%s: mclk: %d\n", __func__,  jaguar1_mclk);
	switch(jaguar1_mclk)
	{
		case 3:
			printk("[DRV] SET_MIPI_1242MHZ\n");
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x40, 0xB4);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x41, 0x00);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x42, 0x03);
			break;	
			//		case 3:
			//		printk("[DRV]_MIPI_252MHZ_TEST_\n");
			//		gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x40, 0xDC);
			//		gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x41, 0x20);
			//		gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x42, 0x05);
			//		break;
		case 2:
			printk("[DRV] SET_MIPI_378MHZ\n");
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x40, 0xDC);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x41, 0x20);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x42, 0x03);
			break;
		case 1:
			printk("[DRV] SET_MIPI_594MHZ\n");
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x40, 0xCC);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x41, 0x10);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x42, 0x03);
			break;
		default:
			printk("[DRV] SET_MIPI_756MHZ\n");
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x40, 0xDC);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x41, 0x10);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x42, 0x03);
			break;
	}

	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x43, 0x43);

	switch(jaguar1_mclk)
	{
		case 3: // 1242MHz MIPI_CLK for FHD*4ch
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x11, 0x08);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x10, 0x13);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x12, 0x0B);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x13, 0x12);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x17, 0x02);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x18, 0x12);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x15, 0x07);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x14, 0x2D);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x16, 0x0B);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x19, 0x09);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1A, 0x15);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1B, 0x11);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1C, 0x0E);
			break;	
		case 2: // 378MHz MIPI_CLK for low-clock test
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x11, 0x03);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x10, 0x07);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x12, 0x04);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x13, 0x06);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x17, 0x01);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x18, 0x0B);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x15, 0x02);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x14, 0x0E);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x16, 0x04);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x19, 0x03);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1A, 0x07);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1B, 0x06);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1C, 0x05);
			break;
		case 1: // 594MHz MIPI_CLK for HD*4ch
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x11, 0x04);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x10, 0x0A);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x12, 0x06);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x13, 0x09);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x17, 0x01);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x18, 0x0D);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x15, 0x04);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x14, 0x16);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x16, 0x05);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x19, 0x05);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1A, 0x0A);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1B, 0x08);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1C, 0x07);
			break;
		default: // 756MHz MIPI_CLK
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x11, 0x05);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x10, 0x0C);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x12, 0x07);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x13, 0x0B);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x17, 0x01);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x18, 0x0E);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x15, 0x04);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x14, 0x1C);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x16, 0x07);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x19, 0x06);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1A, 0x0D);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1B, 0x0B);
			gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x1C, 0x09);
			break;
	}

	// MIPI setting
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x44, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x49, 0xF3);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x49, 0xF0);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x44, 0x02);

	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x08, 0x40);

	// MIPI_TX_FRAME_CNT_EN
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x0F, 0x01);

	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x38, mipi_dtype);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x39, mipi_dtype);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x3A, mipi_dtype);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x3B, mipi_dtype);

	// MIPI Enable
	if(2 == jaguar1_lane)
	{
		gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x07, 0x07);  //two lanes test
		gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x2D, 0x00);
		printk("NOTE >>> 2 lanes mode enabled\n");
	}
	else
	{
		gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x07, 0x0F);
		gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0x2D, 0x01);
	}
	
	printk("[DRV]VDEC_MIPI_TX_INIT done\n");
}

void disable_parallel(int dev_num)
{
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xFF, 0x01);

	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xC8, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xC9, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xCA, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xCB, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xCC, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xCD, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xCE, 0x00);
	gpio_i2c_write(jaguar1_i2c_addr[dev_num], 0xCF, 0x00);

	printk("[DRV]Parallel block Disable\n");
}

