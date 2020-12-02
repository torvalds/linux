// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
*
*  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
*  Copyright (c) 2021 	Rockchip Electronics Co. Ltd.All rights reserved.
*  Module		: install driver main
*  Description	: driver main
*  Author		:
*  Date         :
*  Version		: Version 2.0
*
********************************************************************************
*  History      :
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

#ifndef CONFIG_HISI_SNAPSHOT_BOOT
#include <linux/miscdevice.h>
#endif

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <asm/io.h>
//#include <asm/system.h>
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

//#include "gpio_i2c.h"
#include "nvp6158_video.h"
#include "nvp6158_coax_protocol.h"
#include "nvp6158_motion.h"
#include "nvp6158_common.h"
#include "nvp6158_audio.h"
#include "nvp6158_video_auto_detect.h"
//#include "acp_firmup.h"
#include "nvp6158_video_eq.h"
#include "nvp6158_drv.h"

//#define STREAM_ON_DEFLAULT
/*BT601 is not used by Nextchip */
//#define BT601
#define BT1120

#define AF_CNT	1

#ifdef CONFIG_HISI_SNAPSHOT_BOOT
#include "himedia.h"
#define DEV_NAME "nvp6158"
#endif

static struct i2c_board_info nvp6158_hi_info =
{
    I2C_BOARD_INFO("nvp6158", 0x60),
};
static bool nvp6158_init_state;
unsigned int nvp6158_gCoaxFirmUpdateFlag[16] = {0,};
unsigned char nvp6158_det_mode[16] = {NVP6158_DET_MODE_AUTO,};
struct semaphore nvp6158_lock;
extern unsigned char nvp6158_ch_mode_status[16];
extern unsigned char nvp6158_ch_vfmt_status[16];
extern unsigned char nvp6158_acp_isp_wr_en[16];

#define NVP6158_DRIVER_VER "1.1.01"
#define NVP6158_HW_REG(reg)         *((volatile unsigned int *)(reg))

int nvp6158_g_soc_chiptype = 0x3521;
int nvp6158_chip_id[4];
int nvp6158_rev_id[4];

unsigned int nvp6158_cnt = 0;
unsigned int nvp6158_iic_addr[4] = {0x60, 0x62, 0x64, 0x66};
struct i2c_client* nvp6158_client;

/*******************************************************************************
*	Description		: Get rev ID
*	Argurments		: dec(slave address)
*	Return value	: rev ID
*	Modify			:
*	warning			:
*******************************************************************************/
static int nvp6158_check_rev(unsigned int dec)
{
	int ret;
	gpio_i2c_write(dec, 0xFF, 0x00);
	ret = gpio_i2c_read(dec, 0xf5);
	return ret;
}

/*******************************************************************************
*	Description		: Get Device ID
*	Argurments		: dec(slave address)
*	Return value	: Device ID
*	Modify			:
*	warning			:
*******************************************************************************/
static int nvp6158_check_id(unsigned int dec)
{
	int ret;
	gpio_i2c_write(dec, 0xFF, 0x00);
	ret = gpio_i2c_read(dec, 0xf4);
	return ret;
}

/*******************************************************************************
 *	Description		: Check decoder count
 *	Argurments		: void
 *	Return value	: (total chip count - 1) or -1(not found any chip)
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static int nvp6158_check_decoder_count(void)
{
	int chip;
	int ret = -1;

    /* check Device ID of maxium 4chip on the slave address,
     * manage slave address. chip count. */
	for(chip = 0; chip < 4; chip ++) {
		nvp6158_chip_id[chip] = nvp6158_check_id(nvp6158_iic_addr[chip]);
		nvp6158_rev_id[chip]  = nvp6158_check_rev(nvp6158_iic_addr[chip]);
		if( (nvp6158_chip_id[chip] != NVP6158_R0_ID ) && (nvp6158_chip_id[chip] != NVP6158C_R0_ID) &&
			(nvp6158_chip_id[chip] != NVP6168_R0_ID ) && (nvp6158_chip_id[chip] != NVP6168C_R0_ID)) {
			printk("[NVP6158_DRV]Device ID Error... 0x%x\n", nvp6158_chip_id[chip]);
		} else {
			printk("[NVP6158_DRV]Device (0x%x) ID OK... 0x%x\n", nvp6158_iic_addr[chip], nvp6158_chip_id[chip]);
			printk("[NVP6158_DRV]Device (0x%x) REV ... 0x%x\n", nvp6158_iic_addr[chip], nvp6158_rev_id[chip]);
			nvp6158_iic_addr[nvp6158_cnt] = nvp6158_iic_addr[chip];
			if(nvp6158_cnt<chip)
				nvp6158_iic_addr[chip] = 0xFF;
			nvp6158_chip_id[nvp6158_cnt] = nvp6158_chip_id[chip];
			nvp6158_rev_id[nvp6158_cnt]  = nvp6158_rev_id[chip];
			nvp6158_cnt++;
		}
	}

	printk("[NVP6158_DRV]Chip Count = %d\n", nvp6158_cnt);
	printk("[NVP6158_DRV]Address [0x%x][0x%x][0x%x][0x%x]\n", nvp6158_iic_addr[0],
			nvp6158_iic_addr[1], nvp6158_iic_addr[2], nvp6158_iic_addr[3]);
	printk("[NVP6158_DRV]Chip Id [0x%x][0x%x][0x%x][0x%x]\n", nvp6158_chip_id[0],
			nvp6158_chip_id[1], nvp6158_chip_id[2], nvp6158_chip_id[3]);
	printk("[NVP6158_DRV]Rev Id [0x%x][0x%x][0x%x][0x%x]\n", nvp6158_rev_id[0],
			nvp6158_rev_id[1], nvp6158_rev_id[2], nvp6158_rev_id[3]);

	ret = nvp6158_cnt;

	return ret;
}

/*******************************************************************************
 *	Description		: Video decoder initial
 *	Argurments		: void
 *	Return value	: void
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static void nvp6158_video_decoder_init(void)
{
	int chip = 0;
	unsigned char ch = 0;
	video_input_auto_detect vin_auto_det;
#ifdef _NVP6168_USE_MANUAL_MODE_
	video_input_manual_mode vin_manual_det;
#endif

	printk("[NVP6158_DRV] %s(%d) \n", __func__, __LINE__);
	/* initialize common value of AHD */
	for(chip = 0; chip < nvp6158_cnt; chip++) {
		nvp6158_common_init(chip);
		if(nvp6158_chip_id[chip] == NVP6158C_R0_ID || nvp6158_chip_id[chip] == NVP6158_R0_ID) {
			nvp6158_additional_for3MoverDef(chip);
		} else {
			gpio_i2c_write(nvp6158_iic_addr[chip], 0xff, 0x01 );
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x97, 0x00); // CH_RST ON
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x97, 0x0f); // CH_RST OFF
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x7a, 0x0f); // Clock Auto ON
			gpio_i2c_write(nvp6158_iic_addr[chip], 0xca, 0xff); // VCLK_EN, VDO_EN

			for(ch = 0; ch < 4; ch++) {
				gpio_i2c_write(nvp6158_iic_addr[chip], 0xff, 0x05 + ch);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x00, 0xd0);

				gpio_i2c_write(nvp6158_iic_addr[chip], 0x05, 0x04);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x08, 0x55);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x47, 0xEE);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x59, 0x00);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x76, 0x00);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x77, 0x80);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x78, 0x00);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x79, 0x11);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0xB8, 0xB8); // H_PLL_BYPASS
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x7B, 0x11); // v_rst_on
				gpio_i2c_write(nvp6158_iic_addr[chip], 0xb9, 0x72);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0xB8, 0xB8); // No Video Set

				gpio_i2c_write(nvp6158_iic_addr[chip], 0xff, 0x00);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x00+ch, 0x10);
				gpio_i2c_write(nvp6158_iic_addr[chip], 0x22+(ch*0x04), 0x0b);
			}

			gpio_i2c_write(nvp6158_iic_addr[chip], 0xff, 0x13);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x12, 0x04);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x2E, 0x10);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x30, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x3a, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x3b, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x3c, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x3d, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x3e, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x3f, 0x0f);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x70, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x72, 0x05);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x7A, 0xf0);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x61, 0x03);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x62, 0x00);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x63, 0x03);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x64, 0x00);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x65, 0x03);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x66, 0x00);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x67, 0x03);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x68, 0x00);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x60, 0x0f);
//				gpio_i2c_write(nvp6158_iic_addr[chip], 0x60, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x07, 0x47);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x59, 0x24);

			/* SAM Range */
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x74, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x76, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x78, 0x00);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x75, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x77, 0xff);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x79, 0xff);

			gpio_i2c_write(nvp6158_iic_addr[chip], 0x01, 0x0c);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x2f, 0xc8);

			// EQ Stage Get
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x73, 0x23);

			gpio_i2c_write(nvp6158_iic_addr[chip], 0xff, 0x09);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x96, 0x03);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0xB6, 0x03);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0xD6, 0x03);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0xF6, 0x03);

			/********************************************************
			 * Audio Default Setting
			 ********************************************************/
			gpio_i2c_write(nvp6158_iic_addr[chip], 0xff, 0x01);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x05, 0x09);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x58, 0x02);
			gpio_i2c_write(nvp6158_iic_addr[chip], 0x59, 0x00);

		}
	}

	for(ch = 0; ch < nvp6158_cnt * 4; ch++) {
		nvp6158_det_mode[ch] = NVP6158_DET_MODE_AUTO;
		vin_auto_det.ch = ch % 4;
		vin_auto_det.devnum = ch / 4;
	#ifdef _NVP6168_USE_MANUAL_MODE_
		vin_manual_det.ch = ch % 4;
		vin_manual_det.dev_num = ch / 4;
	#endif
		if(nvp6158_chip_id[0] == NVP6158C_R0_ID || nvp6158_chip_id[0] == NVP6158_R0_ID) {
			nvp6158_video_input_auto_detect_set(&vin_auto_det);
			nvp6158_set_chnmode(ch, NC_VIVO_CH_FORMATDEF_UNKNOWN);
		} else {
			nvp6168_video_input_auto_detect_set(&vin_auto_det);
		#ifdef _NVP6168_USE_MANUAL_MODE_
			nvp6168_video_input_manual_mode_set(&vin_manual_det);
		#endif
			nvp6168_set_chnmode(ch, NC_VIVO_CH_FORMATDEF_UNKNOWN);
		}
	}

	for(chip = 0; chip < nvp6158_cnt; chip++) {
		if(nvp6158_chip_id[chip] == NVP6158_R0_ID || nvp6158_chip_id[chip] == NVP6168_R0_ID) {
			//set nvp6158 output mode of 4port, 0~3 port is available
			nvp6158_set_portmode(chip, 0, NVP6158_OUTMODE_1MUX_FHD, 0);
			nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_FHD, 1);
			nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_FHD, 2);
			nvp6158_set_portmode(chip, 3, NVP6158_OUTMODE_1MUX_FHD, 3);
		} else {//if(nvp6158_chip_id[chip] == NVP6158C_R0_ID)
			//set nvp6158C output mode of 2port, 1/2 port is available
			nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_2MUX_FHD, 0);
			nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_2MUX_FHD, 1);
		}
	}

}


int nvp6158_open(struct inode * inode, struct file * file)
{
	printk("[DRV] Nvp6158 Driver Open\n");
	printk("[DRV] Nvp6158 Driver Ver::%s\n", NVP6158_DRIVER_VER);
	return 0;
}

int nvp6158_close(struct inode * inode, struct file * file)
{
	printk("[DRV] Nvp6158 Driver Close\n");
	return 0;
}

unsigned int nvp6158_g_vloss=0xFFFF;

long nvp6158_native_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int __user *argp = (unsigned int __user *)arg;
	int cpy2usr_ret;
	unsigned char i;
	//unsigned char oCableDistance = 0;
	video_equalizer_info_s s_eq_dist;
	nvp6158_opt_mode optmode;
	//nvp6158_video_mode vmode;
	nvp6158_chn_mode schnmode;
	nvp6158_video_adjust v_adj;
	NVP6158_INFORMATION_S vfmt;
	nvp6158_coax_str coax_val;
	nvp6158_input_videofmt_ch vfmt_ch;
	nvp6124_i2c_mode i2c;
	FIRMWARE_UP_FILE_INFO coax_fw_val;
	motion_mode motion_set;
	//int ret=0;


	/* you must skip other command to improve speed of f/w update
	 * when you are updating cam's f/w up. we need to review and test */
	//if( acp_dvr_checkFWUpdateStatus( cmd ) == -1 )
	//{
		//printk(">>>>> DRV[%s:%d] Now cam f/w update mode. so Skip other command.\n", __func__, __LINE__ );
		//return 0;
	//}
	down(&nvp6158_lock);
	switch (cmd) {
		case IOC_VDEC_SET_I2C : // nextchip demoboard test
			if (copy_from_user(&i2c, argp, sizeof(nvp6124_i2c_mode))) {
				up(&nvp6158_lock);
				return -1;
			}

			if(i2c.flag == 0) {// read
				gpio_i2c_write(i2c.slaveaddr, 0xFF, i2c.bank);
				i2c.data = gpio_i2c_read(i2c.slaveaddr, i2c.address);
			} else {//write
				gpio_i2c_write(i2c.slaveaddr, 0xFF, i2c.bank);
				gpio_i2c_write(i2c.slaveaddr, i2c.address, i2c.data);
			}
			if(copy_to_user(argp, &i2c, sizeof(nvp6124_i2c_mode)))
				printk("IOC_VDEC_I2C error\n");
		break;
		case IOC_VDEC_GET_VIDEO_LOSS: // Not use
			//nvp6158_g_vloss = nvp6158_getvideoloss();
			if(copy_to_user(argp, &nvp6158_g_vloss, sizeof(unsigned int)))
				printk("IOC_VDEC_GET_VIDEO_LOSS error\n");
			break;
		case IOC_VDEC_GET_EQ_DIST:
        		if (copy_from_user(&s_eq_dist, argp, sizeof(video_equalizer_info_s))) {
				up(&nvp6158_lock);
				return -1;
			}

			s_eq_dist.distance = nvp6158_get_eq_dist(&s_eq_dist);
			if(copy_to_user(argp, &s_eq_dist, sizeof(video_equalizer_info_s)))
				printk("IOC_VDEC_GET_EQ_DIST error\n");
			break;
		case IOC_VDEC_SET_EQUALIZER:
			if (copy_from_user(&s_eq_dist, argp, sizeof(video_equalizer_info_s))) {
				up(&nvp6158_lock);
				return -1;
			}
			if(nvp6158_chip_id[0] == NVP6158C_R0_ID || nvp6158_chip_id[0] == NVP6158_R0_ID)
				nvp6158_set_equalizer(&s_eq_dist);
			else
				nvp6168_set_equalizer(&s_eq_dist);
			break;
		case IOC_VDEC_GET_DRIVERVER:
			if(copy_to_user(argp, &NVP6158_DRIVER_VER, sizeof(NVP6158_DRIVER_VER)))
				printk("IOC_VDEC_GET_DRIVERVER error\n");
			break;
		case IOC_VDEC_ACP_WRITE:
			/*if (copy_from_user(&ispdata, argp, sizeof(nvp6158_acp_rw_data)))
				return -1;
			if(ispdata.opt == 0)
				acp_isp_write(ispdata.ch, ispdata.addr, ispdata.data);
			else
			{
				ispdata.data = acp_isp_read(ispdata.ch, ispdata.addr);
				if(copy_to_user(argp, &ispdata, sizeof(nvp6158_acp_rw_data)))
					printk("IOC_VDEC_ACP_WRITE error\n");
			}*/
			break;
		case IOC_VDEC_ACP_WRITE_EXTENTION:

			break;
		case IOC_VDEC_PTZ_ACP_READ:
			//if (copy_from_user(&vfmt, argp, sizeof(nvp6158_input_videofmt)))
			//	return -1;
			//for(i=0;i<(4*nvp6158_cnt);i++)
			//{
			//	if(1)
			//	{
					/* read A-CP */
					//if(((nvp6158_g_vloss>>i)&0x01) == 0x00)
					//	acp_read(&vfmt, i);
			//	}
			//}
			//if(copy_to_user(argp, &vfmt, sizeof(nvp6158_input_videofmt)))
			//	printk("IOC_VDEC_PTZ_ACP_READ error\n");
			break;
		case IOC_VDEC_PTZ_ACP_READ_EACH_CH:
			if (copy_from_user(&vfmt_ch, argp, sizeof(nvp6158_input_videofmt_ch))) {
				up(&nvp6158_lock);
				return -1;
			}
			/* read A-CP */
			if(((nvp6158_g_vloss>>vfmt_ch.ch) & 0x01) == 0x00) {
				//acp_read(&vfmt_ch.vfmt, vfmt_ch.ch);
			}

			if(copy_to_user(argp, &vfmt_ch, sizeof(nvp6158_input_videofmt_ch)))
				printk("IOC_VDEC_PTZ_ACP_READ_EACH_CH error\n");
			break;
		case IOC_VDEC_GET_INPUT_VIDEO_FMT:
			if (copy_from_user(&vfmt, argp, sizeof(NVP6158_INFORMATION_S))) {
				up(&nvp6158_lock);
				return -1;
			}
			if(nvp6158_chip_id[0] == NVP6158C_R0_ID || nvp6158_chip_id[0] == NVP6158_R0_ID)
				nvp6158_video_fmt_det(vfmt.ch, &vfmt);
			else
				nvp6168_video_fmt_det(vfmt.ch, &vfmt);
			if(copy_to_user(argp, &vfmt, sizeof(NVP6158_INFORMATION_S)))
				printk("IOC_VDEC_GET_INPUT_VIDEO_FMT error\n");
			break;
		case IOC_VDEC_SET_CHDETMODE:
			if (copy_from_user(&nvp6158_det_mode, argp, sizeof(unsigned char) * 16)) {
				up(&nvp6158_lock);
				return -1;
			}
			for(i = 0; i<(nvp6158_cnt * 4); i++) {
				printk("IOC_VDEC_SET_CHNMODE nvp6158_det_mode[%d]==%d\n",
						i, nvp6158_det_mode[i]);
				if(nvp6158_chip_id[0] == NVP6158C_R0_ID || nvp6158_chip_id[0] == NVP6158_R0_ID)
					nvp6158_set_chnmode(i, NC_VIVO_CH_FORMATDEF_UNKNOWN);
				else
					nvp6168_set_chnmode(i, NC_VIVO_CH_FORMATDEF_UNKNOWN);
			}
			break;
		case IOC_VDEC_SET_CHNMODE:
			if (copy_from_user(&schnmode, argp, sizeof(nvp6158_chn_mode))) {
				up(&nvp6158_lock);
				return -1;
			}
			if(nvp6158_chip_id[0] == NVP6158C_R0_ID || nvp6158_chip_id[0] == NVP6158_R0_ID) {
				if(0 == nvp6158_set_chnmode(schnmode.ch, schnmode.chmode))
					printk("IOC_VDEC_SET_CHNMODE OK\n");
			} else {
				if(0 == nvp6168_set_chnmode(schnmode.ch, schnmode.chmode))
					printk("IOC_VDEC_SET_CHNMODE OK\n");
			}
			break;
		case IOC_VDEC_SET_OUTPORTMODE:
            		if(copy_from_user(&optmode, argp, sizeof(nvp6158_opt_mode))) {
				up(&nvp6158_lock);
				return -1;
			}
			nvp6158_set_portmode(optmode.chipsel, optmode.portsel, optmode.portmode, optmode.chid);
			break;
		case IOC_VDEC_SET_BRIGHTNESS:
            		if(copy_from_user(&v_adj, argp, sizeof(nvp6158_video_adjust))) {
				up(&nvp6158_lock);
				return -1;
			}
			//nvp6158_video_set_brightness(v_adj.ch, v_adj.value, nvp6158_ch_vfmt_status[v_adj.ch]);
			break;
		case IOC_VDEC_SET_CONTRAST:
			if(copy_from_user(&v_adj, argp, sizeof(nvp6158_video_adjust))) {
				up(&nvp6158_lock);
				return -1;
			}
			//nvp6158_video_set_contrast(v_adj.ch, v_adj.value, nvp6158_ch_vfmt_status[v_adj.ch]);
			break;
		case IOC_VDEC_SET_HUE:
			if(copy_from_user(&v_adj, argp, sizeof(nvp6158_video_adjust))) {
				up(&nvp6158_lock);
				return -1;
			}
			//nvp6158_video_set_hue(v_adj.ch, v_adj.value, nvp6158_ch_vfmt_status[v_adj.ch]);
			break;
		case IOC_VDEC_SET_SATURATION:
			if(copy_from_user(&v_adj, argp, sizeof(nvp6158_video_adjust))) {
				up(&nvp6158_lock);
				return -1;
			}
			//nvp6158_video_set_saturation(v_adj.ch, v_adj.value, nvp6158_ch_vfmt_status[v_adj.ch]);
			break;
		case IOC_VDEC_SET_SHARPNESS:
			if(copy_from_user(&v_adj, argp, sizeof(nvp6158_video_adjust))) {
				up(&nvp6158_lock);
				return -1;
			}
			nvp6158_video_set_sharpness(v_adj.ch, v_adj.value);
			break; 
		/*----------------------- Coaxial Protocol ----------------------*/
		case IOC_VDEC_COAX_TX_INIT:   //SK_CHANGE 170703
			if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
				printk("IOC_VDEC_COAX_TX_INIT error\n");
			nvp6158_coax_tx_init(&coax_val);
				break;
		case IOC_VDEC_COAX_TX_16BIT_INIT:   //SK_CHANGE 170703
			if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
				printk("IOC_VDEC_COAX_TX_INIT error\n");
			nvp6158_coax_tx_16bit_init(&coax_val);
				break;
		case IOC_VDEC_COAX_TX_CMD_SEND: //SK_CHANGE 170703
			if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
				printk(" IOC_VDEC_COAX_TX_CMD_SEND error\n");
			nvp6158_coax_tx_cmd_send(&coax_val);
				break;
		case IOC_VDEC_COAX_TX_16BIT_CMD_SEND: //SK_CHANGE 170703
			if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
				printk(" IOC_VDEC_COAX_TX_CMD_SEND error\n");
			nvp6158_coax_tx_16bit_cmd_send(&coax_val);
				break;
			case IOC_VDEC_COAX_TX_CVI_NEW_CMD_SEND: //SK_CHANGE 170703
				if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
					printk(" IOC_VDEC_COAX_TX_CMD_SEND error\n");
				nvp6158_coax_tx_cvi_new_cmd_send(&coax_val);
					break;
		case IOC_VDEC_COAX_RX_INIT:
			if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
				printk(" IOC_VDEC_COAX_RX_INIT error\n");
			nvp6158_coax_rx_init(&coax_val);
			break;
		case IOC_VDEC_COAX_RX_DATA_READ:
			if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
			printk(" IOC_VDEC_COAX_RX_DATA_READ error\n");
			nvp6158_coax_rx_data_get(&coax_val);
			cpy2usr_ret = copy_to_user(argp, &coax_val, sizeof(nvp6158_coax_str));
			break;
		case IOC_VDEC_COAX_RX_BUF_CLEAR:
			if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
				printk(" IOC_VDEC_COAX_RX_BUF_CLEAR error\n");
			nvp6158_coax_rx_buffer_clear(&coax_val);
			break;
		case IOC_VDEC_COAX_RX_DEINIT:
			if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
				printk("IOC_VDEC_COAX_RX_DEINIT error\n");
			nvp6158_coax_rx_deinit(&coax_val);
			break;
		/*=============== Coaxial Protocol A-CP Option ===============*/
		case IOC_VDEC_COAX_RT_NRT_MODE_CHANGE_SET:
			if(copy_from_user(&coax_val, argp, sizeof(nvp6158_coax_str)))
			printk(" IOC_VDEC_COAX_SHOT_SET error\n");
			nvp6158_coax_option_rt_nrt_mode_change_set(&coax_val);
			cpy2usr_ret = copy_to_user(argp, &coax_val, sizeof(nvp6158_coax_str));
			break;
		/*=========== Coaxial Protocol Firmware Update ==============*/
		case IOC_VDEC_COAX_FW_ACP_HEADER_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_READY_CMD_SET error\n");
			nvp6158_coax_fw_ready_header_check_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_READY_CMD_SET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_READY_CMD_SET error\n");
			nvp6158_coax_fw_ready_cmd_to_isp_send(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_READY_ACK_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_READY_ISP_STATUS_GET error\n");
			nvp6158_coax_fw_ready_cmd_ack_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_START_CMD_SET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			nvp6158_coax_fw_start_cmd_to_isp_send(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_START_ACK_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			nvp6158_coax_fw_start_cmd_ack_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_SEND_DATA_SET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			nvp6158_coax_fw_one_packet_data_to_isp_send(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_SEND_ACK_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			nvp6158_coax_fw_one_packet_data_ack_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_END_CMD_SET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			nvp6158_coax_fw_end_cmd_to_isp_send(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		case IOC_VDEC_COAX_FW_END_ACK_GET:
			if(copy_from_user(&coax_fw_val, argp, sizeof(FIRMWARE_UP_FILE_INFO)))
				printk("IOC_VDEC_COAX_FW_START_CMD_SET error\n");
			nvp6158_coax_fw_end_cmd_ack_from_isp_recv(&coax_fw_val);
			cpy2usr_ret = copy_to_user(argp, &coax_fw_val, sizeof(FIRMWARE_UP_FILE_INFO));
			break;
		/*=========== Coaxial Protocol Firmware Update END ==============*/
		/*----------------------- MOTION ----------------------*/
		case IOC_VDEC_MOTION_DETECTION_GET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_SET error\n");
			nvp6158_motion_detection_get(&motion_set);
			cpy2usr_ret = copy_to_user(argp, &motion_set, sizeof(motion_mode));
		break;
		case IOC_VDEC_MOTION_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_SET error\n");
			nvp6158_motion_onoff_set(&motion_set);
			break;
		case IOC_VDEC_MOTION_PIXEL_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_Pixel_SET error\n");
			nvp6158_motion_pixel_onoff_set(&motion_set);
		break;
		case IOC_VDEC_MOTION_PIXEL_GET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_Pixel_SET error\n");
			nvp6158_motion_pixel_onoff_get(&motion_set);
			cpy2usr_ret = copy_to_user(argp, &motion_set, sizeof(motion_mode));
			break;
		case IOC_VDEC_MOTION_ALL_PIXEL_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_Pixel_SET error\n");
			nvp6158_motion_pixel_all_onoff_set(&motion_set);
		break;
		case IOC_VDEC_MOTION_TSEN_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_TSEN_SET error\n");
			nvp6158_motion_tsen_set(&motion_set);
		break;
		case IOC_VDEC_MOTION_PSEN_SET :
			if(copy_from_user(&motion_set, argp, sizeof(motion_set)))
				printk("IOC_VDEC_MOTION_PSEN_SET error\n");
			nvp6158_motion_psen_set(&motion_set);
		break;
		default:
            //printk("drv:invalid nc decoder ioctl cmd[%x]\n", cmd);
			break;
	}
	up(&nvp6158_lock);
	return 0;
}

#ifdef TEST
void nvp6158_set_bt656_601_mode(void)
{
	unsigned char ch = 0;
	int chip = 0;

	printk("[NVP6158_DRV] %s(%d) \n", __func__, __LINE__);
	for(ch = 0; ch < nvp6158_cnt * 4; ch++) {
		nvp6158_set_chnmode(ch, AHD20_1080P_30P);
	}

	for(chip = 0; chip < nvp6158_cnt; chip++) {
		nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_FHD, 0);
		nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_FHD, 1);
	}
	//VDO1 diabled
	gpio_i2c_write(0x60, 0xFF, 0x01);
	gpio_i2c_write(0x60, 0xCA, 0x64);
#ifdef BT601
	//BT601 test
	gpio_i2c_write(0x60, 0xFF, 0x01);
	gpio_i2c_write(0x60, 0xA8, 0x80); //BT601 out
	gpio_i2c_write(0x60, 0xAB, 0x80); //BT601 out
	gpio_i2c_write(0x60, 0xBD, 0x00); //BT601 VSYNC HSYNC
	gpio_i2c_write(0x60, 0xBE, 0x00); //BT601 VSYNC HSYNC
	gpio_i2c_write(0x60, 0xA9, 0x10); //CH1 Signal out for BT601 (MPP1 = V_BLK1, MPP2=H_BLK1)
	gpio_i2c_write(0x60, 0xAA, 0x20); //CH2 Signal out for BT601 (MPP3 = V_BLK1, MPP4=H_BLK1)
#endif
}

void nvp6158_set_bt1120_720P_mode(void)
{
	unsigned char ch = 0;
	int chip = 0;

	printk("[NVP6158_DRV] %s(%d) \n", __func__, __LINE__);
	for(ch = 0; ch < nvp6158_cnt * 4; ch++) {
		nvp6158_set_chnmode(ch, AHD20_720P_30P);
	}

	for(chip = 0; chip < nvp6158_cnt; chip ++) {
		nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_BT1120S_720P, 1);
		nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_BT1120S_720P, 1);
	}
}

void nvp6158_set_bt1120_1080P_mode(void)
{
	unsigned char ch = 0;
	int chip = 0;

	printk("[NVP6158_DRV] %s(%d) \n", __func__, __LINE__);
	for(ch = 0; ch < nvp6158_cnt * 4; ch++) {
		nvp6158_set_chnmode(ch, AHD20_1080P_30P);
	}

	for(chip = 0; chip < nvp6158_cnt; chip++) {
		nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_BT1120S_1080P, 0);
		nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_BT1120S_1080P, 0);
	}
}
#endif

void nvp6158_start(video_init_all *video_init, bool dual_edge)
{
	unsigned char ch = 0;
	int chip = 0;
	NC_VIVO_CH_FORMATDEF fmt_idx;
	NVP6158_DVP_MODE mode;

	fmt_idx = video_init->ch_param[0].format;
	mode = video_init->mode;
	down(&nvp6158_lock);
	nvp6158_video_decoder_init();
	/* initialize Audio
	 * recmaster, pbmaster, ch_num, samplerate, bits */
	if(nvp6158_chip_id[0] == NVP6158C_R0_ID || nvp6158_chip_id[0] == NVP6158_R0_ID)
		nvp6158_audio_init(1,0,16,0,0);
	else
		nvp6168_audio_init(1,0,16,0,0);

	switch (fmt_idx) {
		/* normal output */
		case AHD20_720P_25P:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++)
				nvp6158_set_chnmode(ch, AHD20_720P_25P);
			break;
		case AHD20_720P_30P:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++)
				nvp6158_set_chnmode(ch, AHD20_720P_30P);
			break;
		case AHD20_1080P_25P:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++)
				nvp6158_set_chnmode(ch, AHD20_1080P_25P);
			break;
		case AHD20_1080P_30P:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++)
				nvp6158_set_chnmode(ch, AHD20_1080P_30P);
			break;
		case AHD30_3M_18P:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++) {
				if (dual_edge)
					nvp6158_set_chnmode(ch, AHD30_3M_30P);
				else
					nvp6158_set_chnmode(ch, AHD30_3M_18P);

			}
			break;
		case AHD30_4M_15P:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++) {
				if (dual_edge)
					nvp6158_set_chnmode(ch, AHD30_4M_30P);
				else
					nvp6158_set_chnmode(ch, AHD30_4M_15P);
			}
			break;
		case AHD30_5M_12_5P:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++) {
				if (dual_edge)
					nvp6158_set_chnmode(ch, AHD30_5M_20P);
				else
					nvp6158_set_chnmode(ch, AHD30_5M_12_5P);
			}
			break;
		case AHD30_8M_7_5P:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++) {
				if (dual_edge)
					nvp6158_set_chnmode(ch, AHD30_8M_15P);
				else
					nvp6158_set_chnmode(ch, AHD30_8M_7_5P);
			}
			break;

		/* test output */
		case AHD20_SD_SH720_NT:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++)
				nvp6158_set_chnmode(ch, AHD20_SD_SH720_NT); /* 720*480i*/
			break;
		case AHD20_SD_SH720_PAL:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++)
				nvp6158_set_chnmode(ch, AHD20_SD_SH720_PAL); /* 720*576i*/
			break;
		case AHD20_SD_H960_PAL:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++)
				nvp6158_set_chnmode(ch, AHD20_SD_H960_PAL); /* 960*576i*/
			break;
		case AHD20_SD_H960_EX_PAL:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++)
				nvp6158_set_chnmode(ch, AHD20_SD_H960_EX_PAL); /*1920*576i*/
			break;
		default:
			for (ch = 0; ch < nvp6158_cnt * 4; ch++)
				nvp6158_set_chnmode(ch, AHD20_1080P_30P);
			break;
	}

	nvp6158_set_colorpattern();
	//nvp6158_set_colorpattern2();
	//nvp6158_set_colorpattern3();

	switch (mode) {
		/* normal output */
		case BT656_1MUX:
			if ((fmt_idx == AHD20_1080P_25P) || (fmt_idx == AHD20_1080P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_FHD, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_FHD, 1);
				}
			} else if ((fmt_idx == AHD20_720P_25P) || (fmt_idx == AHD20_720P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_HD, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_HD, 1);
				}
			} else if ((fmt_idx == AHD30_3M_18P) || (fmt_idx == AHD30_4M_15P) ||
					(fmt_idx == AHD30_5M_12_5P) || (fmt_idx == AHD30_8M_7_5P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_FHD, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_FHD, 1);
				}
			} else if ((fmt_idx == AHD20_1080P_50P) || (fmt_idx == AHD20_1080P_60P) ||
					(fmt_idx == AHD30_3M_30P) || (fmt_idx == AHD30_4M_30P) ||
					(fmt_idx == AHD30_3M_25P) || (fmt_idx == AHD30_4M_25P) ||
					(fmt_idx == AHD30_5M_20P) || (fmt_idx == AHD30_8M_15P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_FHD, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_FHD, 1);
				}
			}
			//standard sync head
			gpio_i2c_write(0x60, 0xFF, 0x00);
			gpio_i2c_write(0x60, 0x54, 0x00);
			//VDO2/VDO1 enabled VCLK_1/2_EN
			gpio_i2c_write(0x60, 0xFF, 0x01);
			gpio_i2c_write(0x60, 0xCA, 0x66);
			break;
		case BT656_2MUX:
			if ((fmt_idx == AHD20_1080P_25P) || (fmt_idx == AHD20_1080P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_2MUX_FHD, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_2MUX_FHD, 0);
				}
			} else if ((fmt_idx == AHD20_720P_25P) || (fmt_idx == AHD20_720P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_2MUX_HD, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_2MUX_HD, 0);
				}
			}
			//standard sync head
			gpio_i2c_write(0x60, 0xFF, 0x00);
			gpio_i2c_write(0x60, 0x54, 0x00);
			//VDO2 enabled VDO1 disabled VCLK_1_EN
			gpio_i2c_write(0x60, 0xFF, 0x01);
			gpio_i2c_write(0x60, 0xCA, 0x66);
			break;
		case BT1120_1MUX:
			if ((fmt_idx == AHD20_1080P_25P) || (fmt_idx == AHD20_1080P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_BT1120S_1080P, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_BT1120S_1080P, 0);
				}
			} else if ((fmt_idx == AHD20_720P_25P) || (fmt_idx == AHD20_720P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_BT1120S_720P, 1);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_BT1120S_720P, 1);
				}
			}
			//VDO2/VDO1 enabled VCLK_1_EN/VCLK_2_EN
			gpio_i2c_write(0x60, 0xFF, 0x01);
			gpio_i2c_write(0x60, 0xCA, 0x66);
			break;
		case BT1120_2MUX:
			if ((fmt_idx == AHD20_1080P_25P) || (fmt_idx == AHD20_1080P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_2MUX_BT1120S_1080P, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_2MUX_BT1120S_1080P, 1);
				}
			} else if ((fmt_idx == AHD20_720P_25P) || (fmt_idx == AHD20_720P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_2MUX_BT1120S_720P, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_2MUX_BT1120S_720P, 1);
				}
			}
			//VDO2/VDO1 enabled VCLK_1_EN/VCLK_2_EN
			gpio_i2c_write(0x60, 0xFF, 0x01);
			gpio_i2c_write(0x60, 0xCA, 0x66);
			break;
		case BT1120_4MUX:
			if ((fmt_idx == AHD20_1080P_25P) || (fmt_idx == AHD20_1080P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_4MUX_BT1120S_1080P, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_4MUX_BT1120S_1080P, 1);
				}
			} else if ((fmt_idx == AHD20_720P_25P) || (fmt_idx == AHD20_720P_30P)) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					if (dual_edge) {
						nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_4MUX_BT1120S_DDR, 0);
						nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_4MUX_BT1120S_DDR, 1);
					} else {
						nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_4MUX_BT1120S, 0);
						nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_4MUX_BT1120S, 1);
					}
				}
			}
			//VDO2/VDO1 enabled VCLK_1_EN/VCLK_2_EN
			gpio_i2c_write(0x60, 0xFF, 0x01);
			gpio_i2c_write(0x60, 0xCA, 0x66);
			break;
		/* test output */
		case BT656I_TEST_MODES:
			if (fmt_idx == AHD20_SD_H960_EX_PAL) {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_HD, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_HD, 1);
				}
			} else {
				for (chip = 0; chip < nvp6158_cnt; chip++) {
					nvp6158_set_portmode(chip, 1, NVP6158_OUTMODE_1MUX_SD, 0);
					nvp6158_set_portmode(chip, 2, NVP6158_OUTMODE_1MUX_SD, 1);
				}
			}
			//VDO2 enabled VDO1 disabled VCLK_1_EN
			gpio_i2c_write(0x60, 0xFF, 0x01);
			//gpio_i2c_write(0x60, 0xCA, 0x64);
			//VDO2/VDO1 enabled VCLK_1_EN/VCLK_2_EN
			gpio_i2c_write(0x60, 0xCA, 0x66);
			break;
		default:
			printk("mode %d not supported yet\n", mode);
			break;
	}
	up(&nvp6158_lock);
}

void nvp6158_stop(void)
{
	unsigned char ch = 0;

	down(&nvp6158_lock);
	//VDO_1/2 disabled, VCLK_x disabled
	gpio_i2c_write(0x60, 0xFF, 0x01);
	gpio_i2c_write(0x60, 0xCA, 0x00);

	for(ch = 0; ch < 4;ch++) {
		nvp6158_channel_reset(ch);
	}
	up(&nvp6158_lock);
}

/*******************************************************************************
 *	Description		: i2c client initial
 *	Argurments		: int
 *	Return value	: 0
 *	Modify			:
 *	warning			:
 *******************************************************************************/
static int nvp6158_i2c_client_init(int i2c_bus)
{
    struct i2c_adapter* i2c_adap;

    printk("[DRV] I2C Client Init \n");
    i2c_adap = i2c_get_adapter(i2c_bus);

    nvp6158_client = i2c_new_client_device(i2c_adap, &nvp6158_hi_info);
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
void nvp6158_i2c_client_exit(void)
{
    i2c_unregister_device(nvp6158_client);
}

int nvp6158_init(int i2c_bus)
{
	int ret = 0;
#ifdef FMT_SETTING_SAMPLE
	int dev_num = 0;
#endif

	if (nvp6158_init_state)
		return 0;

	ret = nvp6158_i2c_client_init(i2c_bus);
	if (ret) {
		printk(KERN_ERR "ERROR: could not find nvp6158\n");
		return ret;
	}

	/* decoder count function */
	ret = nvp6158_check_decoder_count();
	if (ret <= 0) {
		printk(KERN_ERR "ERROR: could not find nvp6158 devices:%#x\n", ret);
		nvp6158_i2c_client_exit();
		return -ENODEV;
	}

	/* initialize semaphore */
	sema_init(&nvp6158_lock, 1);
	nvp6158_init_state = true;

	return 0;
}

void nvp6158_exit(void)
{
	nvp6158_i2c_client_exit();
	nvp6158_init_state = false;
}

/*******************************************************************************
*	End of file
*******************************************************************************/

