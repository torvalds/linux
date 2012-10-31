/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * Semaphore stuff seems quite broken in here. --libv
 */
#include "drv_hdmi_i.h"
#include "hdmi_hal.h"
#include "dev_hdmi.h"
#include "../disp/dev_disp.h"

/*
 * Bad separation!
 * symbol from sound/soc/sun[45]i/hdmiaudio/sndhdmi.c
 */
extern void audio_set_hdmi_func(__audio_hdmi_func *hdmi_func);


static struct semaphore *run_sem;
static struct task_struct *HDMI_task;

void hdmi_delay_ms(__u32 t)
{
	__u32 timeout = t * HZ / 1000;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(timeout);
}

__s32 Hdmi_open(void)
{
	__inf("[Hdmi_open]\n");

	Hdmi_hal_video_enable(1);

#if 0
	if (ghdmi.bopen == 0)
		up(run_sem);
#endif

	ghdmi.bopen = 1;

	return 0;
}

__s32 Hdmi_close(void)
{
	__inf("[Hdmi_close]\n");

	Hdmi_hal_video_enable(0);
	ghdmi.bopen = 0;

	return 0;
}

__s32 Hdmi_set_display_mode(__disp_tv_mode_t mode)
{
	__u32 hdmi_mode;

	__inf("[Hdmi_set_display_mode],mode:%d\n", mode);

	switch (mode) {
	case DISP_TV_MOD_480I:
		hdmi_mode = HDMI1440_480I;
		break;

	case DISP_TV_MOD_576I:
		hdmi_mode = HDMI1440_576I;
		break;

	case DISP_TV_MOD_480P:
		hdmi_mode = HDMI480P;
		break;

	case DISP_TV_MOD_576P:
		hdmi_mode = HDMI576P;
		break;

	case DISP_TV_MOD_720P_50HZ:
		hdmi_mode = HDMI720P_50;
		break;

	case DISP_TV_MOD_720P_60HZ:
		hdmi_mode = HDMI720P_60;
		break;

	case DISP_TV_MOD_1080I_50HZ:
		hdmi_mode = HDMI1080I_50;
		break;

	case DISP_TV_MOD_1080I_60HZ:
		hdmi_mode = HDMI1080I_60;
		break;

	case DISP_TV_MOD_1080P_24HZ:
		hdmi_mode = HDMI1080P_24;
		break;

	case DISP_TV_MOD_1080P_50HZ:
		hdmi_mode = HDMI1080P_50;
		break;

	case DISP_TV_MOD_1080P_60HZ:
		hdmi_mode = HDMI1080P_60;
		break;

	case DISP_TV_MOD_1080P_24HZ_3D_FP:
		hdmi_mode = HDMI1080P_24_3D_FP;
		break;

	case DISP_TV_MOD_720P_50HZ_3D_FP:
		hdmi_mode = HDMI720P_50_3D_FP;
		break;

	case DISP_TV_MOD_720P_60HZ_3D_FP:
		hdmi_mode = HDMI720P_60_3D_FP;
		break;

	default:
		__wrn("unsupported video mode %d when set display mode\n",
		      mode);
		return -1;
	}

	ghdmi.mode = mode;
	return Hdmi_hal_set_display_mode(hdmi_mode);
}

__s32 Hdmi_Audio_Enable(__u8 mode, __u8 channel)
{
	__inf("[Hdmi_Audio_Enable],ch:%d\n", channel);

	return Hdmi_hal_audio_enable(mode, channel);
}

__s32 Hdmi_Set_Audio_Para(hdmi_audio_t *audio_para)
{
	__inf("[Hdmi_Set_Audio_Para]\n");

	return Hdmi_hal_set_audio_para(audio_para);
}

__s32 Hdmi_mode_support(__disp_tv_mode_t mode)
{
	__u32 hdmi_mode;

	switch (mode) {
	case DISP_TV_MOD_480I:
		hdmi_mode = HDMI1440_480I;
		break;

	case DISP_TV_MOD_576I:
		hdmi_mode = HDMI1440_576I;
		break;

	case DISP_TV_MOD_480P:
		hdmi_mode = HDMI480P;
		break;

	case DISP_TV_MOD_576P:
		hdmi_mode = HDMI576P;
		break;

	case DISP_TV_MOD_720P_50HZ:
		hdmi_mode = HDMI720P_50;
		break;

	case DISP_TV_MOD_720P_60HZ:
		hdmi_mode = HDMI720P_60;
		break;

	case DISP_TV_MOD_1080I_50HZ:
		hdmi_mode = HDMI1080I_50;
		break;

	case DISP_TV_MOD_1080I_60HZ:
		hdmi_mode = HDMI1080I_60;
		break;

	case DISP_TV_MOD_1080P_24HZ:
		hdmi_mode = HDMI1080P_24;
		break;

	case DISP_TV_MOD_1080P_50HZ:
		hdmi_mode = HDMI1080P_50;
		break;

	case DISP_TV_MOD_1080P_60HZ:
		hdmi_mode = HDMI1080P_60;
		break;

	case DISP_TV_MOD_1080P_24HZ_3D_FP:
		hdmi_mode = HDMI1080P_24_3D_FP;
		break;

	case DISP_TV_MOD_720P_50HZ_3D_FP:
		hdmi_mode = HDMI720P_50_3D_FP;
		break;

	case DISP_TV_MOD_720P_60HZ_3D_FP:
		hdmi_mode = HDMI720P_60_3D_FP;
		break;

	default:
		hdmi_mode = HDMI720P_50;
		break;
	}

	return Hdmi_hal_mode_support(hdmi_mode);
}

__s32 Hdmi_get_HPD_status(void)
{
	return Hdmi_hal_get_HPD();
}

static __s32
Hdmi_set_pll(__u32 pll, __u32 clk)
{
	Hdmi_hal_set_pll(pll, clk);
	return 0;
}

static int
Hdmi_run_thread(void *parg)
{
	while (1) {
		Hdmi_hal_main_task();

		if (kthread_should_stop())
			break;

		hdmi_delay_ms(2000);
	}

	return 0;
}

__s32 Hdmi_init(void)
{
	__audio_hdmi_func audio_func;
	__disp_hdmi_func disp_func;

	run_sem = kmalloc(sizeof(struct semaphore), GFP_KERNEL | __GFP_ZERO);
	sema_init((struct semaphore *)run_sem, 0);

	HDMI_task = kthread_create(Hdmi_run_thread, (void *)0, "hdmi proc");
	if (IS_ERR(HDMI_task)) {
		__s32 err = 0;

		__wrn("Unable to start kernel thread %s.\n", "hdmi proc");
		err = PTR_ERR(HDMI_task);
		HDMI_task = NULL;
		return err;
	}
	wake_up_process(HDMI_task);

	Hdmi_set_reg_base((void __iomem *) ghdmi.base_hdmi);
	Hdmi_hal_init();

	audio_func.hdmi_audio_enable = Hdmi_Audio_Enable;
	audio_func.hdmi_set_audio_para = Hdmi_Set_Audio_Para;
	audio_set_hdmi_func(&audio_func);

	disp_func.Hdmi_open = Hdmi_open;
	disp_func.Hdmi_close = Hdmi_close;
	disp_func.hdmi_set_mode = Hdmi_set_display_mode;
	disp_func.hdmi_mode_support = Hdmi_mode_support;
	disp_func.hdmi_get_HPD_status = Hdmi_get_HPD_status;
	disp_func.hdmi_set_pll = Hdmi_set_pll;
	disp_set_hdmi_func(&disp_func);

	return 0;
}

__s32 Hdmi_exit(void)
{
	Hdmi_hal_exit();

	kfree(run_sem);
	run_sem = NULL;

	if (HDMI_task) {
		kthread_stop(HDMI_task);
		HDMI_task = NULL;
	}

	return 0;
}
