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

unsigned int nvp6158_mode = PAL;  //0:ntsc, 1: pal
unsigned int nvp6158_kthread = 0;

module_param(nvp6158_mode, uint, S_IRUGO);

module_param(nvp6158_kthread, uint, S_IRUGO);
#ifdef CONFIG_HISI_SNAPSHOT_BOOT
static struct himedia_device s_stNvp6158Device;
#endif

#ifdef CONFIG_HISI_SNAPSHOT_BOOT
static int nvp6158_freeze(struct himedia_device* pdev)
{
    printk(KERN_ALERT "%s  %d\n", __FUNCTION__, __LINE__);
    return 0;
}

static int nvp6158_restore(struct himedia_device* pdev)
{
    printk(KERN_ALERT "%s  %d\n", __FUNCTION__, __LINE__);
    return 0;
}
#endif

static const struct file_operations nvp6158_fops = {
	.owner      = THIS_MODULE,
    	.unlocked_ioctl	= nvp6158_native_ioctl,
	.open       = nvp6158_open,
	.release    = nvp6158_close,
};

#ifdef CONFIG_HISI_SNAPSHOT_BOOT
struct himedia_ops stNvp6158DrvOps =
{
    .pm_freeze = nvp6158_freeze,
    .pm_restore  = nvp6158_restore
};
#endif

/*******************************************************************************
*	Description		: kernel thread for EQ (now, not used)
*	Argurments		: void
*	Return value	: 0
*	Modify			:
*	warning			:
*******************************************************************************/
#ifdef STREAM_ON_DEFLAULT
static int nvp6158_kernel_thread(void *data)
{
	//int ch;
	//nvp6158_input_videofmt videofmt;
	NVP6158_INFORMATION_S s_nvp6158_vfmts;
	video_equalizer_info_s s_eq_info;
	unsigned char prefmt=0, curfmt=0, chvloss=0;
	unsigned char ch = 0;

	memset(&s_nvp6158_vfmts, 0, sizeof(NVP6158_INFORMATION_S));

	while(!kthread_should_stop()) {
		#if 1  //standard rutine of a process
		down(&nvp6158_lock);
		ch = ch % (nvp6158_cnt*4);
		nvp6158_getvideoloss();
		if(nvp6158_chip_id[0]==NVP6158C_R0_ID || nvp6158_chip_id[0]==NVP6158_R0_ID) {
			nvp6158_video_fmt_det(ch, &s_nvp6158_vfmts);
			curfmt = s_nvp6158_vfmts.curvideofmt[ch];
			prefmt = s_nvp6158_vfmts.prevideofmt[ch];
			chvloss = s_nvp6158_vfmts.curvideoloss[ch];
			//printk(">>>>>>%s CH[%d] chvloss = %d curfmt = %x prefmt = %x\n", __func__, ch, chvloss, curfmt, prefmt);

			if(chvloss == 0x00) {	
				if(nvp6158_ch_mode_status[ch] != prefmt) {
					nvp6158_set_chnmode(ch, curfmt);
					nvp6158_set_portmode(0, ch%4, NVP6158_OUTMODE_1MUX_FHD, ch%4);
					s_eq_info.Ch = ch%4;
					s_eq_info.devnum = ch/4;
					s_eq_info.FmtDef = curfmt;
					nvp6158_get_eq_dist(&s_eq_info);
					s_nvp6158_vfmts.prevideofmt[ch] = curfmt;
					printk(">>>>>>%s CH[%d] s_eq_info.distance = %d\n",
						__func__, ch, s_eq_info.distance);
					nvp6158_set_equalizer(&s_eq_info);
					
				}
			} else {	
				if(nvp6158_ch_mode_status[ch] != NC_VIVO_CH_FORMATDEF_UNKNOWN) {
					nvp6158_set_chnmode(ch, NC_VIVO_CH_FORMATDEF_UNKNOWN);
					nvp6158_set_portmode(0, ch%4, NVP6158_OUTMODE_1MUX_FHD, ch%4);
				}
			}
		} else {
			nvp6168_video_fmt_det(ch, &s_nvp6158_vfmts);
			curfmt = s_nvp6158_vfmts.curvideofmt[ch];
			prefmt = s_nvp6158_vfmts.prevideofmt[ch];
			chvloss = s_nvp6158_vfmts.curvideoloss[ch];
			//printk(">>>>>>%s CH[%d] chvloss = %d curfmt = %x prefmt = %x nvp6158_ch_mode_status[%d]=%x\n", __func__, ch, chvloss, curfmt, prefmt, ch, nvp6158_ch_mode_status[ch]);

			if(chvloss == 0x00) {
				if(nvp6158_ch_mode_status[ch] != prefmt) {
					nvp6168_set_chnmode(ch, curfmt);
					nvp6158_set_portmode(0, ch%4, NVP6158_OUTMODE_1MUX_FHD, ch%4);
					s_eq_info.Ch = ch%4;
					s_eq_info.devnum = ch/4;
					s_eq_info.FmtDef = curfmt;
					nvp6158_get_eq_dist(&s_eq_info);
					s_nvp6158_vfmts.prevideofmt[ch] = curfmt;
					printk(">>>>>>%s CH[%d] s_eq_info.distance = %d\n", __func__, ch, s_eq_info.distance);
					nvp6168_set_equalizer(&s_eq_info);
				}
			} else {
				if(nvp6158_ch_mode_status[ch] != NC_VIVO_CH_FORMATDEF_UNKNOWN) {
					nvp6168_set_chnmode(ch, NC_VIVO_CH_FORMATDEF_UNKNOWN);
					nvp6158_set_portmode(0, ch%4, NVP6158_OUTMODE_1MUX_FHD, ch%4);
				}
			}
		}
		ch ++;
		up(&nvp6158_lock);
		#endif
		schedule_timeout_interruptible(msecs_to_jiffies(200));
		//printk("nvp6158_kernel_thread running\n");
	}

	return 0;
}
#endif

/*******************************************************************************
*	Description		: It is called when "insmod nvp61XX_ex.ko" command run
*	Argurments		: void
*	Return value	: -1(could not register nvp61XX device), 0(success)
*	Modify			:
*	warning			:
*******************************************************************************/
struct proc_dir_entry *nvp6158_dir;
#define NVP6158_PROC_ENTRY "nvp6158"

#ifndef CONFIG_VIDEO_NVP6158

#ifdef STREAM_ON_DEFLAULT
static struct task_struct *nvp6158_kt = NULL;
#endif

#ifndef CONFIG_HISI_SNAPSHOT_BOOT
static struct miscdevice nvp6158_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "nc_vdec",
	.fops  		= &nvp6158_fops,
};
#endif

static int __init nvp6158_module_init(void)
{
	int ret = 0;
	//char entry[20];
#ifdef CONFIG_HISI_SNAPSHOT_BOOT
	snprintf(s_stNvp6158Device.devfs_name, sizeof(s_stNvp6158Device.devfs_name), DEV_NAME);
	s_stNvp6158Device.minor  = HIMEDIA_DYNAMIC_MINOR;
	s_stNvp6158Device.fops   = &nvp6158_fops;
	s_stNvp6158Device.drvops = &stNvp6158DrvOps;
	s_stNvp6158Device.owner  = THIS_MODULE;

	ret = himedia_register(&s_stNvp6158Device);
	if (ret) {
		printk(0, "could not register nvp6158_dev device");
		return -1;
 	}
#else
	ret = misc_register(&nvp6158_dev);
   	if (ret) {
		printk("ERROR: could not register nvp6158_dev devices:%#x \n",ret);
        	return -1;
	}
#endif

#ifdef STREAM_ON_DEFLAULT
	//printk("NVP6158/68(C) ex Driver %s COMPILE TIME[%s %s]\n", NVP6158_DRIVER_VER, __DATE__,__TIME__);
	nvp6158_init(I2C_1);
	if (ret) {
		printk(KERN_ERR "ERROR: jaguar1 init failed\n");
		return -1;
	}
	down(&nvp6158_lock);
	nvp6158_video_decoder_init();
	/* initialize Audio
	 * recmaster, pbmaster, ch_num, samplerate, bits */
	if(nvp6158_chip_id[0] == NVP6158C_R0_ID || nvp6158_chip_id[0] == NVP6158_R0_ID)
		nvp6158_audio_init(1, 0, 16, 0, 0);
	else
		nvp6168_audio_init(1, 0, 16, 0, 0);
	//VDO_1/2 enable, VCLK_x ebable
	gpio_i2c_write(0x60, 0xFF, 0x01);
	gpio_i2c_write(0x60, 0xCA, 0x66);
	up(&nvp6158_lock);

	/* create kernel thread for EQ, But Now not used. */
	if(nvp6158_kthread == 1) {
		nvp6158_kt = kthread_create(nvp6158_kernel_thread, NULL, "nvp6158_kt");
	    	if(!IS_ERR(nvp6158_kt))
	       		wake_up_process(nvp6158_kt);
	    	else {
	        	printk("create nvp6158 watchdog thread failed!!\n");
	        	nvp6158_kt = 0;
	        	return 0;
	    	}
	}
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
static void __exit nvp6158_module_exit(void)
{
#ifdef STREAM_ON_DEFLAULT
	if(nvp6158_kt)
		kthread_stop(nvp6158_kt);
#endif
#ifdef CONFIG_HISI_SNAPSHOT_BOOT
	himedia_unregister(&s_stNvp6158Device);
#else
	misc_deregister(&nvp6158_dev);
#endif
	nvp6158_i2c_client_exit();
	//printk("NVP6158(C) ex Driver %s COMPILE TIME[%s %s] removed\n", NVP6158_DRIVER_VER, __DATE__,__TIME__);
}

module_init(nvp6158_module_init);
module_exit(nvp6158_module_exit);
#endif

MODULE_LICENSE("GPL");

/*******************************************************************************
*	End of file
*******************************************************************************/

