// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: jaguar1_drv.c
 *  Description	:
 *  Author		:
 *  Date        :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History     :
 *
 *
 ********************************************************************************/

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/list.h>
#include <asm/delay.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <asm/bitops.h>
#include <linux/uaccess.h>
#include <asm/irq.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "jaguar1_common.h"
#include "jaguar1_video.h"
#include "jaguar1_coax_protocol.h"
#include "jaguar1_motion.h"
#include "jaguar1_ioctl.h"
#include "jaguar1_video_eq.h"
#include "jaguar1_mipi.h"
#include "jaguar1_drv.h"
#ifdef FOR_IMX6
#include "imx_mipi.h"
#endif
//#include "video_eq.h" //To do

//#define STREAM_ON_DEFLAULT

#define I2C_0       (0)
#define I2C_1       (1)
#define I2C_2       (2)
#define I2C_3       (3)

#define JAGUAR1_4PORT_R0_ID 0xB0
#define JAGUAR1_2PORT_R0_ID 0xA0
#define JAGUAR1_1PORT_R0_ID 0xA2
#define AFE_NVP6134E_R0_ID 	0x80

#define JAGUAR1_4PORT_REV_ID 0x00
#define JAGUAR1_2PORT_REV_ID 0x00
#define JAGUAR1_1PORT_REV_ID 0x00

static int chip_id[4];
static int rev_id[4];
static int jaguar1_cnt;
unsigned int jaguar1_i2c_addr[4] = {0x60, 0x62, 0x64, 0x66};
unsigned int jaguar1_mclk = 0; //0:756 1:594 2:378 3:1242
module_param_named(jaguar1_mclk, jaguar1_mclk, uint, S_IRUGO);
unsigned int jaguar1_lane = 4; //2 or 4
module_param_named(jaguar1_lane, jaguar1_lane, uint, S_IRUGO);
static unsigned int chn = 4;
module_param_named(jaguar1_chn, chn, uint, S_IRUGO);
static unsigned int init = 0;
module_param_named(jaguar1_init, init, uint, S_IRUGO);
static unsigned int fmt = 2;  //0:960H;1:720P 2:1080P 3:960P 4:SH720
module_param_named(jaguar1_fmt, fmt, uint, S_IRUGO);
static unsigned int ntpal = 0;
module_param_named(jaguar1_ntpal, ntpal, uint, S_IRUGO);

static bool jaguar1_init_state;
struct semaphore jaguar1_lock;
struct i2c_client* jaguar1_client;
static struct i2c_board_info hi_info =
{
	I2C_BOARD_INFO("jaguar1", 0x60),
};
decoder_get_information_str decoder_inform;

unsigned int acp_mode_enable = 1;
module_param(acp_mode_enable, uint, S_IRUGO);

static void vd_pattern_enable(void)
{
	gpio_i2c_write(0x60, 0xFF, 0x00);
	gpio_i2c_write(0x60, 0x1C, 0x1A);
	gpio_i2c_write(0x60, 0x1D, 0x1A);
	gpio_i2c_write(0x60, 0x1E, 0x1A);
	gpio_i2c_write(0x60, 0x1F, 0x1A);

	gpio_i2c_write(0x60, 0xFF, 0x05);
	gpio_i2c_write(0x60, 0x6A, 0x80);
	gpio_i2c_write(0x60, 0xFF, 0x06);
	gpio_i2c_write(0x60, 0x6A, 0x80);
	gpio_i2c_write(0x60, 0xFF, 0x07);
	gpio_i2c_write(0x60, 0x6A, 0x80);
	gpio_i2c_write(0x60, 0xFF, 0x08);
	gpio_i2c_write(0x60, 0x6A, 0x80);
}

/*******************************************************************************
 *	Description		: Sample function - for select video format
 *	Argurments		: int dev_num(i2c_address array's num)
 *	Return value	: void
 *	Modify			:
 *	warning			:
 *******************************************************************************/
#if 0
static void set_default_video_fmt(int dev_num)
{
#if 0 // Activate this block if a default video-setting is required.
	int i;
	video_input_init  video_val;

	/* default video datatype setting */
	mipi_datatype_set(VD_DATA_TYPE_YUV422);

	/* mipi_tx_initial */
	mipi_tx_init(dev_num);

	/* run default video format setting */
	for( i=0 ; i<4 ; i++)
	{
		video_val.ch = i;

		/* select video format, include struct'vd_vi_init_list' in jaguar1_video_table.h
		 *  ex > AHD20_1080P_30P / AHD20_720P_25P_EX_Btype / AHD20_SD_H960_2EX_Btype_NT */
		video_val.format = AHD20_720P_30P_EX_Btype;
		// select analog input type, SINGLE_ENDED or DIFFERENTIAL
		video_val.input = SINGLE_ENDED;
		// select decoder to soc interface
		video_val.interface = YUV_422;

		// run video setting
		vd_jaguar1_init_set(&video_val);

		// run video format setting for mipi/arbiter
		mipi_video_format_set(&video_val);
		set_imx_video_format(&video_val);
		init_imx_mipi(i);
	}
	arb_init(dev_num);
	disable_parallel(dev_num);
#endif
}
#endif

/*******************************************************************************
 *	Description		: Check ID
 *	Argurments		: dec(slave address)
 *	Return value	: Device ID
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static void vd_set_all(video_init_all *param)
{
	int i, dev_num=0;
	video_input_init  video_val[4];

#if 0
	for(i=0 ; i<4 ; i++)
	{
		printk("[DRV || %s] ch%d / fmt:%d / input:%d / interface:%d\n",__func__
				, param->ch_param[i].ch
				, param->ch_param[i].format
				, param->ch_param[i].input
				, param->ch_param[i].interface);
	}
#endif
	mipi_datatype_set(VD_DATA_TYPE_YUV422); // to do
	mipi_tx_init(dev_num);

	for( i=0 ; i<4 ; i++)
	{
		video_val[i].ch = param->ch_param[i].ch;
		video_val[i].format = param->ch_param[i].format;
		video_val[i].input = param->ch_param[i].input;
		if(i<chn)
			video_val[i].interface = param->ch_param[i].interface;
		else
			video_val[i].interface = DISABLE;

		vd_jaguar1_init_set(&video_val[i]);
		mipi_video_format_set(&video_val[i]);
#ifdef FOR_IMX6
		set_imx_video_format(&video_val[i]);
		if(video_val[i].interface == DISABLE)
		{
			printk("[DRV] Nothing selected [video ch : %d]\n", i);
		}
		else
		{
			init_imx_mipi(i);
		}
#endif
	}
	arb_init(dev_num);
	disable_parallel(dev_num);
	vd_pattern_enable();
}

/*******************************************************************************
 *	Description		: Check ID
 *	Argurments		: dec(slave address)
 *	Return value	: Device ID
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static int check_id(unsigned int dec)
{
	int ret;
	gpio_i2c_write(dec, 0xFF, 0x00);
	ret = gpio_i2c_read(dec, 0xf4);
	return ret;
}

/*******************************************************************************
 *	Description		: Get rev ID
 *	Argurments		: dec(slave address)
 *	Return value	: rev ID
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static int check_rev(unsigned int dec)
{
	int ret;
	gpio_i2c_write(dec, 0xFF, 0x00);
	ret = gpio_i2c_read(dec, 0xf5);
	return ret;
}

/*******************************************************************************
 *	Description		: Check decoder count
 *	Argurments		: void
 *	Return value	: (total chip count - 1) or -1(not found any chip)
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static int check_decoder_count(void)
{
	int chip, i;
	int ret = -1;

	for(chip=0;chip<4;chip++)
	{
		chip_id[chip] = check_id(jaguar1_i2c_addr[chip]);
		rev_id[chip]  = check_rev(jaguar1_i2c_addr[chip]);
		if( (chip_id[chip] != JAGUAR1_4PORT_R0_ID )  	&&
				(chip_id[chip] != JAGUAR1_2PORT_R0_ID) 		&&
				(chip_id[chip] != JAGUAR1_1PORT_R0_ID)		&&
				(chip_id[chip] != AFE_NVP6134E_R0_ID)
		  )
		{
			printk("Device ID Error... %x, Chip Count:[%d]\n", chip_id[chip], chip);
			jaguar1_i2c_addr[chip] = 0xFF;
			chip_id[chip] = 0xFF;
		}
		else
		{
			printk("Device (0x%x) ID OK... %x , Chip Count:[%d]\n", jaguar1_i2c_addr[chip], chip_id[chip], chip);
			printk("Device (0x%x) REV %x\n", jaguar1_i2c_addr[chip], rev_id[chip]);
			jaguar1_i2c_addr[jaguar1_cnt] = jaguar1_i2c_addr[chip];

			if(jaguar1_cnt<chip)
			{
				jaguar1_i2c_addr[chip] = 0xFF;
			}

			chip_id[jaguar1_cnt] = chip_id[chip];
			rev_id[jaguar1_cnt]  = rev_id[chip];

			jaguar1_cnt++;
		}

		if((chip == 3) && (jaguar1_cnt < chip))
		{
			for(i = jaguar1_cnt; i < 4; i++)
			{
				chip_id[i] = 0xff;
				rev_id[i]  = 0xff;
			}
		}
	}
	printk("Chip Count = %d\n", jaguar1_cnt);
	printk("Address [0x%x][0x%x][0x%x][0x%x]\n",jaguar1_i2c_addr[0],jaguar1_i2c_addr[1],jaguar1_i2c_addr[2],jaguar1_i2c_addr[3]);
	printk("Chip Id [0x%x][0x%x][0x%x][0x%x]\n",chip_id[0],chip_id[1],chip_id[2],chip_id[3]);
	printk("Rev Id [0x%x][0x%x][0x%x][0x%x]\n",rev_id[0],rev_id[1],rev_id[2],rev_id[3]);

	for( i = 0; i < 4; i++ )
	{
		decoder_inform.chip_id[i] = chip_id[i];
		decoder_inform.chip_rev[i] = rev_id[i];
		decoder_inform.chip_addr[i] = jaguar1_i2c_addr[i];
	}
	decoder_inform.Total_Chip_Cnt = jaguar1_cnt;
	ret = jaguar1_cnt;

	return ret;
}

/*******************************************************************************
 *	Description		: Video decoder initial
 *	Argurments		: void
 *	Return value	: void
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static void video_decoder_init(void)
{
	int ii = 0;

	// Pad Control Setting
	gpio_i2c_write(jaguar1_i2c_addr[0], 0xff, 0x04);

	for(ii =0; ii<36; ii++)
	{
		gpio_i2c_write(jaguar1_i2c_addr[0], 0xa0 + ii , 0x24);
	}

	// Clock Delay Setting
	gpio_i2c_write(jaguar1_i2c_addr[0], 0xff, 0x01);
	for(ii =0; ii<4; ii++)
	{
		gpio_i2c_write(jaguar1_i2c_addr[0], 0xcc + ii , 0x64);
	}
#if 1
	// MIPI_V_REG_OFF
	gpio_i2c_write(jaguar1_i2c_addr[0], 0xff, 0x21);
	gpio_i2c_write(jaguar1_i2c_addr[0], 0x07, 0x80);
	gpio_i2c_write(jaguar1_i2c_addr[0], 0x07, 0x00);
#endif

#if 1
	// AGC_OFF  08.31
	gpio_i2c_write(jaguar1_i2c_addr[0], 0xff, 0x0A);
	gpio_i2c_write(jaguar1_i2c_addr[0], 0x77, 0x8F);
	gpio_i2c_write(jaguar1_i2c_addr[0], 0xF7, 0x8F);
	gpio_i2c_write(jaguar1_i2c_addr[0], 0xff, 0x0B);
	gpio_i2c_write(jaguar1_i2c_addr[0], 0x77, 0x8F);
	gpio_i2c_write(jaguar1_i2c_addr[0], 0xF7, 0x8F);
#endif
}

/*******************************************************************************
 *	Description		: Driver open
 *	Argurments		:
 *	Return value	:
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static int jaguar1_open(struct inode * inode, struct file * file)
{
	printk("[DRV] Jaguar1 Driver Open\n");
	printk("[DRV] Jaguar1 Driver Ver::%s\n", DRIVER_VER);
	return 0;
} 

/*******************************************************************************
 *	Description		: Driver close
 *	Argurments		:
 *	Return value	:
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static int jaguar1_close(struct inode * inode, struct file * file)
{
	printk("[DRV] Jaguar1 Driver Close\n");
	return 0;
}

/*******************************************************************************
 *	Description		: Driver IOCTL function
 *	Argurments		:
 *	Return value	:
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static long jaguar1_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int cpy2usr_ret;
	unsigned int __user *argp = (unsigned int __user *)arg;	

	/* AllVideo Variable */
	video_init_all all_vd_val;

	/* Video Variable */
	video_input_init  video_val;
	video_output_init vo_seq_set;
	video_equalizer_info_s video_eq;
	video_video_loss_s vidloss;

	/* Coaxial Protocol Variable */
	NC_VD_COAX_STR           coax_val;
	NC_VD_COAX_BANK_DUMP_STR coax_bank_dump;
	FIRMWARE_UP_FILE_INFO    coax_fw_val;
	NC_VD_COAX_TEST_STR      coax_test_val;

	/* Motion Variable */
	motion_mode motion_set;

	nc_acp_rw_data ispdata;

	down(&jaguar1_lock);

	switch (cmd)
	{
		/*===============================================================================================
		 * Set All - for MIPI Interface
		 *===============================================================================================*/
		case IOC_VDEC_INIT_ALL:
			if(copy_from_user(&all_vd_val, argp, sizeof(video_init_all)))
				printk("IOC_VDEC_INPUT_INIT error\n");
			vd_set_all(&all_vd_val);
			break;
		/*===============================================================================================
		 * Video Initialize
		 *===============================================================================================*/
		case IOC_VDEC_INPUT_INIT:
			if(copy_from_user(&video_val, argp, sizeof(video_input_init)))
				printk("IOC_VDEC_INPUT_INIT error\n");
			vd_jaguar1_init_set(&video_val);
			break;
		case IOC_VDEC_OUTPUT_SEQ_SET:
			if(copy_from_user(&vo_seq_set, argp, sizeof(video_output_init)))
				printk("IOC_VDEC_INPUT_INIT error\n");
			vd_jaguar1_vo_ch_seq_set(&vo_seq_set);
			break;
		case IOC_VDEC_VIDEO_EQ_SET:
			if(copy_from_user(&video_eq, argp, sizeof(video_equalizer_info_s)))
				printk("IOC_VDEC_INPUT_INIT error\n");
			video_input_eq_val_set(&video_eq);
			break;
		case IOC_VDEC_VIDEO_SW_RESET:
			if(copy_from_user(&video_val, argp, sizeof(video_input_init)))
				printk("IOC_VDEC_INPUT_INIT error\n");
			vd_jaguar1_sw_reset(&video_val);
			break;
		case IOC_VDEC_VIDEO_EQ_CABLE_SET:
			if(copy_from_user(&video_eq, argp, sizeof(video_equalizer_info_s)))
				printk("IOC_VDEC_INPUT_INIT error\n");
			video_input_eq_cable_set(&video_eq);
			break;
		case IOC_VDEC_VIDEO_EQ_ANALOG_INPUT_SET:
			if(copy_from_user(&video_eq, argp, sizeof(video_equalizer_info_s)))
				printk("IOC_VDEC_INPUT_INIT error\n");
			video_input_eq_analog_input_set(&video_eq);
			break;
		case IOC_VDEC_VIDEO_GET_VIDEO_LOSS:
			if(copy_from_user(&vidloss, argp, sizeof(video_video_loss_s)))
				printk("IOC_VDEC_VIDEO_GET_VIDEO_LOSS error\n");
			vd_jaguar1_get_novideo(&vidloss);
			cpy2usr_ret = copy_to_user(argp, &vidloss, sizeof(video_video_loss_s));
			break;
			/*===============================================================================================
			 * Coaxial Protocol
			 *===============================================================================================*/
		case IOC_VDEC_COAX_TX_INIT:   //SK_CHANGE 170703
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk("IOC_VDEC_COAX_TX_INIT error\n");
			coax_tx_init(&coax_val);
			break;
		case IOC_VDEC_COAX_TX_16BIT_INIT:   //SK_CHANGE 170703
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk("IOC_VDEC_COAX_TX_INIT error\n");
			coax_tx_16bit_init(&coax_val);
			break;
		case IOC_VDEC_COAX_TX_CMD_SEND: //SK_CHANGE 170703
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk(" IOC_VDEC_COAX_TX_CMD_SEND error\n");
			coax_tx_cmd_send(&coax_val);
			break;
		case IOC_VDEC_COAX_TX_16BIT_CMD_SEND: //SK_CHANGE 170703
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk(" IOC_VDEC_COAX_TX_CMD_SEND error\n");
			coax_tx_16bit_cmd_send(&coax_val);
			break;
		case IOC_VDEC_COAX_TX_CVI_NEW_CMD_SEND: //SK_CHANGE 170703
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk(" IOC_VDEC_COAX_TX_CMD_SEND error\n");
			coax_tx_cvi_new_cmd_send(&coax_val);
			break;
		case IOC_VDEC_COAX_RX_INIT:
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk(" IOC_VDEC_COAX_RX_INIT error\n");
			coax_rx_init(&coax_val);
			break;
		case IOC_VDEC_COAX_RX_DATA_READ:
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk(" IOC_VDEC_COAX_RX_DATA_READ error\n");
			coax_rx_data_get(&coax_val);
			cpy2usr_ret = copy_to_user(argp, &coax_val, sizeof(NC_VD_COAX_STR));
			break;
		case IOC_VDEC_COAX_RX_BUF_CLEAR:
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk(" IOC_VDEC_COAX_RX_BUF_CLEAR error\n");
			coax_rx_buffer_clear(&coax_val);
			break;
		case IOC_VDEC_COAX_RX_DEINIT:
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk("IOC_VDEC_COAX_RX_DEINIT error\n");
			coax_rx_deinit(&coax_val);
			break;
		case IOC_VDEC_COAX_BANK_DUMP_GET:
			if(copy_from_user(&coax_bank_dump, argp, sizeof(NC_VD_COAX_BANK_DUMP_STR)))
				printk("IOC_VDEC_COAX_BANK_DUMP_GET error\n");
			coax_test_Bank_dump_get(&coax_bank_dump);
			cpy2usr_ret = copy_to_user(argp, &coax_bank_dump, sizeof(NC_VD_COAX_BANK_DUMP_STR));
			break;
		case IOC_VDEC_COAX_RX_DETECTION_READ:
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk(" IOC_VDEC_COAX_RX_DATA_READ error\n");
			coax_acp_rx_detect_get(&coax_val);
			cpy2usr_ret = copy_to_user(argp, &coax_val, sizeof(NC_VD_COAX_STR));
			break;
			/*===============================================================================================
			 * Coaxial Protocol. Function
			 *===============================================================================================*/
		case IOC_VDEC_COAX_RT_NRT_MODE_CHANGE_SET:
			if(copy_from_user(&coax_val, argp, sizeof(NC_VD_COAX_STR)))
				printk(" IOC_VDEC_COAX_SHOT_SET error\n");
			coax_option_rt_nrt_mode_change_set(&coax_val);
			cpy2usr_ret = copy_to_user(argp, &coax_val, sizeof(NC_VD_COAX_STR));
			break;
		case IOC_VDEC_ACP_WRITE:
			if (copy_from_user(&ispdata, argp, sizeof(nc_acp_rw_data))) {
				up(&jaguar1_lock);
				return -1;
			}
			if(ispdata.opt == 0)
				acp_isp_write(ispdata.ch, ispdata.addr, ispdata.data);
			else
			{
				ispdata.data = acp_isp_read(ispdata.ch, ispdata.addr);
				if(copy_to_user(argp, &ispdata, sizeof(nc_acp_rw_data)))
					printk("IOC_VDEC_ACP_WRITE error\n");	
			}
			break;
			/*===============================================================================================
			 * Coaxial Protocol FW Update
			 *===============================================================================================*/
		case IOC_VDEC_COAX_FW_ACP_HEADER_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_READY_CMD_SET error\n");
			coax_fw_ready_header_check_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_READY_CMD_SET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_READY_CMD_SET error\n");
			coax_fw_ready_cmd_to_isp_send(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_READY_ACK_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_READY_ISP_STATUS_GET error\n");
			coax_fw_ready_cmd_ack_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_START_CMD_SET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			coax_fw_start_cmd_to_isp_send(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_START_ACK_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			coax_fw_start_cmd_ack_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_SEND_DATA_SET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			coax_fw_one_packet_data_to_isp_send(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_SEND_ACK_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			coax_fw_one_packet_data_ack_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_END_CMD_SET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			coax_fw_end_cmd_to_isp_send(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_END_ACK_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			coax_fw_end_cmd_ack_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
			/*===============================================================================================
			 * Test Function
			 *===============================================================================================*/
		case IOC_VDEC_COAX_TEST_TX_INIT_DATA_READ:
			if(copy_from_user(&coax_test_val, argp, sizeof(NC_VD_COAX_TEST_STR)))
				printk("IOC_VDEC_COAX_INIT_SET error\n");
			coax_test_tx_init_read(&coax_test_val);
			cpy2usr_ret = copy_to_user(argp, &coax_test_val, sizeof(NC_VD_COAX_TEST_STR));
			break;
		case IOC_VDEC_COAX_TEST_DATA_SET:
			if(copy_from_user(&coax_test_val, argp, sizeof(NC_VD_COAX_TEST_STR)))
				printk("IOC_VDEC_COAX_TEST_DATA_SET error\n");
			coax_test_data_set(&coax_test_val);
			break;
		case IOC_VDEC_COAX_TEST_DATA_READ:
			if(copy_from_user(&coax_test_val, argp, sizeof(NC_VD_COAX_TEST_STR)))
				printk("IOC_VDEC_COAX_TEST_DATA_SET error\n");
			coax_test_data_get(&coax_test_val);
			cpy2usr_ret = copy_to_user(argp, &coax_test_val, sizeof(NC_VD_COAX_TEST_STR));
			break;
			/*===============================================================================================
			 * Motion
			 *===============================================================================================*/
		case IOC_VDEC_MOTION_DETECTION_GET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_SET error\n");
			motion_detection_get(&motion_set);
			cpy2usr_ret = copy_to_user(argp, &motion_set, sizeof(motion_mode));
			break;
		case IOC_VDEC_MOTION_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_SET error\n");
			motion_onoff_set(&motion_set);
			break;
		case IOC_VDEC_MOTION_PIXEL_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_Pixel_SET error\n");
			motion_pixel_onoff_set(&motion_set);
			break;
		case IOC_VDEC_MOTION_PIXEL_GET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_Pixel_SET error\n");
			motion_pixel_onoff_get(&motion_set);
			cpy2usr_ret = copy_to_user(argp, &motion_set, sizeof(motion_mode));
			break;
		case IOC_VDEC_MOTION_ALL_PIXEL_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_Pixel_SET error\n");
			motion_pixel_all_onoff_set(&motion_set);
			break;
		case IOC_VDEC_MOTION_TSEN_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_TSEN_SET error\n");
			motion_tsen_set(&motion_set);
			break;
		case IOC_VDEC_MOTION_PSEN_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_PSEN_SET error\n");
			motion_psen_set(&motion_set);
			break;
			/*===============================================================================================
			 * Version
			 *===============================================================================================*/
		case IOC_VDEC_GET_DRIVERVER :
			if(copy_to_user(argp, &DRIVER_VER, sizeof(DRIVER_VER)))
				printk("IOC_VDEC_GET_DRIVERVER error\n");
			break;
	}

	up(&jaguar1_lock);

	return 0;
}

/*
 * mclk
 * default: 756MHZ
 *       1: 378MHZ
 *       2: 594MHZ
 *	 3: 1242MHZ
 */
void jaguar1_set_mclk(unsigned int mclk)
{
	jaguar1_mclk = mclk;
}

void jaguar1_start(video_init_all *video_init)
{
	down(&jaguar1_lock);
	vd_set_all(video_init);
	up(&jaguar1_lock);
}

void jaguar1_stop(void)
{
	video_input_init  video_val;

	down(&jaguar1_lock);
	arb_disable(0);
	gpio_i2c_write(0x60, 0xff, 0x20);

	// ARB RESET High
	gpio_i2c_write(0x60, 0x40, 0x11);
	usleep_range(3000, 5000);
	gpio_i2c_write(0x60, 0x40, 0x00);
	vd_jaguar1_sw_reset(&video_val);
	up(&jaguar1_lock);
}

/*******************************************************************************
 *	Description		: i2c client initial
 *	Argurments		: void
 *	Return value	: 0
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static int i2c_client_init(int i2c_bus)
{
	struct i2c_adapter* i2c_adap;

	printk("[DRV] I2C Client Init \n");
	i2c_adap = i2c_get_adapter(i2c_bus);
	if (!i2c_adap)
		return -EINVAL;

	jaguar1_client = i2c_new_device(i2c_adap, &hi_info);
	i2c_put_adapter(i2c_adap);

	return 0;
}

/*******************************************************************************
 *	Description		: i2c client release
 *	Argurments		: void
 *	Return value	: void
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static void i2c_client_exit(void)
{
	i2c_unregister_device(jaguar1_client);
}

static struct file_operations jaguar1_fops = {
	.owner      = THIS_MODULE,
	.unlocked_ioctl	= jaguar1_ioctl,
	.open       = jaguar1_open,
	.release    = jaguar1_close
};

static struct miscdevice jaguar1_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "jaguar1",
	.fops  		= &jaguar1_fops,
};

int jaguar1_init(int i2c_bus)
{
	int ret = 0;
#ifdef FMT_SETTING_SAMPLE
	int dev_num = 0;
#endif

	if (jaguar1_init_state)
		return 0;

	ret = i2c_client_init(i2c_bus);
	if (ret) {
		printk(KERN_ERR "ERROR: could not find jaguar1\n");
		return ret;
	}

	/* decoder count function */
	ret = check_decoder_count();
	if (ret <= 0) {
		printk(KERN_ERR "ERROR: could not find jaguar1 devices:%#x\n", ret);
		i2c_client_exit();
		return -ENODEV;
	}

	/* initialize semaphore */
	sema_init(&jaguar1_lock, 1);
	down(&jaguar1_lock);
	video_decoder_init();
	up(&jaguar1_lock);
	jaguar1_init_state = true;

	return 0;
}

void jaguar1_exit(void)
{
	i2c_client_exit();
	jaguar1_init_state = false;
}

/*******************************************************************************
 *	Description		: It is called when "insmod jaguar1.ko" command run
 *	Argurments		: void
 *	Return value	: -1(could not register jaguar1 device), 0(success)
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static int __init jaguar1_module_init(void)
{
	int ret = 0;
#ifdef STREAM_ON_DEFLAULT
	video_init_all sVideoall;
	int ch;

	jaguar1_mclk= 3;
	init = true;
	fmt = 2;
#endif

	ret = misc_register(&jaguar1_dev);
	if (ret)
	{
		printk(KERN_ERR "ERROR: could not register jaguar1-i2c :%#x\n", ret);
		return -1;
	}

#ifdef STREAM_ON_DEFLAULT
	ret = jaguar1_init(I2C_3);
	if (ret)
	{
		printk(KERN_ERR "ERROR: jaguar1 init failed\n");
		return -1;
	}

	down(&jaguar1_lock);
	if(init)
	{
		for(ch=0;ch<jaguar1_cnt*4;ch++)
		{
			sVideoall.ch_param[ch].ch = ch;
			switch(fmt)
			{
				case 0:
					sVideoall.ch_param[ch].format = AHD20_SD_H960_2EX_Btype_NT+ntpal;
				break;
				case 2:
					sVideoall.ch_param[ch].format = AHD20_1080P_25P+ntpal;
				break;
				case 3:
					sVideoall.ch_param[ch].format = AHD20_720P_960P_30P+ntpal;
				break;
				case 4:
					sVideoall.ch_param[ch].format = AHD20_SD_SH720_NT+ntpal;
				break;
				default:
					sVideoall.ch_param[ch].format = AHD20_720P_30P_EX_Btype+ntpal;
				break;
			}
			sVideoall.ch_param[ch].input = SINGLE_ENDED;
			if(ch<chn)
				sVideoall.ch_param[ch].interface = YUV_422;
			else
				sVideoall.ch_param[ch].interface = DISABLE;
		}
		vd_set_all(&sVideoall);
	}

	up(&jaguar1_lock);
#endif

	return 0;
}

/*******************************************************************************
 *	Description		: It is called when "rmmod nvp61XX_ex.ko" command run
 *	Argurments		: void
 *	Return value	: void
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static void __exit jaguar1_module_exit(void)
{
#ifdef FOR_IMX6
	close_imx_mipi();
#endif
	misc_deregister(&jaguar1_dev);

#ifdef STREAM_ON_DEFLAULT
	jaguar1_exit();
#endif

	printk("JAGUAR1 DEVICE DRIVER UNLOAD SUCCESS\n");
}

module_init(jaguar1_module_init);
module_exit(jaguar1_module_exit);

MODULE_LICENSE("GPL");

/*******************************************************************************
 *	End of file
 *******************************************************************************/
