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
#include "hdmi_core.h"
#include "dev_hdmi.h"
#include "../disp/dev_disp.h"


/*
 * Bad separation!
 * symbol from sound/soc/sun[45]i/hdmiaudio/sndhdmi.c
 */
extern void audio_set_hdmi_func(__audio_hdmi_func *hdmi_func);


static struct semaphore *run_sem;
static struct task_struct *HDMI_task;
void __iomem *HDMI_BASE;


static __s32 Hdmi_enable(__bool enable)
{
	if ((video_enable != enable) &&
	    (hdmi_state >= HDMI_State_Video_config))
		hdmi_state = HDMI_State_Video_config;

	video_enable = enable;

	return 0;
}

__s32 hdmi_wait_edid(void)
{
	unsigned long start = jiffies;

	while (time_before(jiffies, start + 10 * HZ)) { /* Wait max 10 sec */
		if (hdmi_state > HDMI_State_EDID_Parse) {
			pr_info("waited %ld ms for EDID info\n",
				(jiffies - start) * 1000 / HZ);
			if (!Device_Support_VIC[HDMI_EDID]) {
				pr_warn("No valid EDID mode found\n");
				return -1;
			}
			return 0;
		}
		hdmi_delay_ms(1);
	}
	pr_warn("Timeout waiting for EDID info\n");
	return -1;
}

__s32 Hdmi_open(void)
{
	__inf("[Hdmi_open]\n");

	Hdmi_enable(1);

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

	Hdmi_enable(0);
	ghdmi.bopen = 0;

	return 0;
}

/* Translate a fex tv-mode into a VIC as used in the hdmi code */
static __u32 Hdmi_tv_mode_to_hdmi_mode(__disp_tv_mode_t mode)
{
	switch (mode) {
	case DISP_TV_MOD_480I:
		return HDMI1440_480I;
	case DISP_TV_MOD_576I:
		return HDMI1440_576I;
	case DISP_TV_MOD_480P:
		return HDMI480P;
	case DISP_TV_MOD_576P:
		return HDMI576P;
	case DISP_TV_MOD_720P_50HZ:
		return HDMI720P_50;
	case DISP_TV_MOD_720P_60HZ:
		return HDMI720P_60;
	case DISP_TV_MOD_1080I_50HZ:
		return HDMI1080I_50;
	case DISP_TV_MOD_1080I_60HZ:
		return HDMI1080I_60;
	case DISP_TV_MOD_1080P_24HZ:
		return HDMI1080P_24;
	case DISP_TV_MOD_1080P_50HZ:
		return HDMI1080P_50;
	case DISP_TV_MOD_1080P_60HZ:
		return HDMI1080P_60;
	case DISP_TV_MOD_1080P_24HZ_3D_FP:
		return HDMI1080P_24_3D_FP;
	case DISP_TV_MOD_720P_50HZ_3D_FP:
		return HDMI720P_50_3D_FP;
	case DISP_TV_MOD_720P_60HZ_3D_FP:
		return HDMI720P_60_3D_FP;
	case DISP_TV_MOD_H1360_V768_60HZ:
		return HDMI1360_768_60;
	case DISP_TV_MOD_H1280_V1024_60HZ:
		return HDMI1280_1024_60;
	case DISP_TV_MODE_EDID:
		if (!Device_Support_VIC[HDMI_EDID]) {
			pr_err("EDID mode used without valid EDID info\n");
			return 0;
		}
		return HDMI_EDID;
	default:
		__wrn("unsupported video mode %d\n", mode);
		return 0;
	}
}

__s32 Hdmi_set_display_mode(__disp_tv_mode_t mode)
{
	__u32 hdmi_mode;

	__inf("[Hdmi_set_display_mode],mode:%d\n", mode);

	hdmi_mode = Hdmi_tv_mode_to_hdmi_mode(mode);
	if (!hdmi_mode)
		return -1;

	ghdmi.mode = mode;
	if (hdmi_mode != video_mode) {
		if (hdmi_state >= HDMI_State_Video_config)
			hdmi_state = HDMI_State_Video_config;

		video_mode = hdmi_mode;
	}
	return 0;
}

__s32 Hdmi_Audio_Enable(__u8 mode, __u8 channel)
{
	__inf("[Hdmi_Audio_Enable],ch:%d\n", channel);

	/* ???????????????????????? */
	if (hdmi_state >= HDMI_State_Audio_config)
		hdmi_state = HDMI_State_Audio_config;

	audio_info.audio_en = (channel == 0) ? 0 : 1;

	return 0;
}

__s32 Hdmi_Set_Audio_Para(hdmi_audio_t *audio_para)
{
	__inf("[Hdmi_Set_Audio_Para]\n");

	if (!audio_para)
		return -1;

	if (audio_para->sample_rate != audio_info.sample_rate) {
		if (hdmi_state >= HDMI_State_Audio_config)
			hdmi_state = HDMI_State_Audio_config;
		audio_info.sample_rate = audio_para->sample_rate;
		/* audio_info.channel_num  = 2; */

		__inf("sample_rate:%d in Hdmi_hal_set_audio_para\n",
		      audio_info.sample_rate);
	}
	if (audio_para->channel_num != audio_info.channel_num) {
		if (hdmi_state >= HDMI_State_Audio_config)
			hdmi_state = HDMI_State_Audio_config;
		audio_info.channel_num = audio_para->channel_num;

		__inf("channel_num:%d in Hdmi_hal_set_audio_para\n",
		      audio_info.channel_num);
	}

	return 0;
}

__s32 Hdmi_mode_support(__disp_tv_mode_t mode)
{
	__u32 hdmi_mode = Hdmi_tv_mode_to_hdmi_mode(mode);

	if (hdmi_mode == 0 || Hpd_Check() == 0)
		return 0;

	while (hdmi_state < HDMI_State_Wait_Video_config)
		hdmi_delay_ms(1);

	return Device_Support_VIC[hdmi_mode];
}

__s32 hdmi_get_video_timing(__disp_tv_mode_t mode,
	struct __disp_video_timing *video_timing_dest)
{
	__u32 hdmi_mode;
	__s32 vic_tab;

	hdmi_mode = Hdmi_tv_mode_to_hdmi_mode(mode);
	if (!hdmi_mode)
		return -1;

	vic_tab = get_video_info(hdmi_mode);
	if (vic_tab == -1)
		return -1;

	memcpy(video_timing_dest, &video_timing[vic_tab],
	       sizeof(struct __disp_video_timing));
	return 0;
}

__s32 Hdmi_get_HPD_status(void)
{
	return Hpd_Check();
}

static __s32
Hdmi_set_pll(__u32 pll, __u32 clk)
{
	hdmi_pll = pll;
	hdmi_clk = clk;
	return 0;
}

static int
Hdmi_run_thread(void *parg)
{
	while (1) {
		hdmi_main_task_loop();

		if (kthread_should_stop())
			break;

		if (hdmi_state == HDMI_State_Wait_Hpd ||
		    hdmi_state == HDMI_State_Playback)
			hdmi_delay_ms(2000);
		else
			hdmi_delay_ms(1);
	}

	return 0;
}

__s32 Hdmi_init(void)
{
	__audio_hdmi_func audio_func;
	__disp_hdmi_func disp_func;

	run_sem = kmalloc(sizeof(struct semaphore), GFP_KERNEL | __GFP_ZERO);
	sema_init((struct semaphore *)run_sem, 0);

	HDMI_BASE = (void __iomem *) ghdmi.base_hdmi;
	hdmi_core_initial();
	audio_info.channel_num = 2;
#if 0
	{ /* for audio test */
		hdmi_audio_t audio_para;

		audio_para.ch0_en = 1;
		audio_para.sample_rate = 44100;
		Hdmi_hal_set_audio_para(&audio_para);

		Hdmi_hal_audio_enable(0, 1);
	}
#endif


	/* Run main task once, should give EDID information directly */
	hdmi_main_task_loop();

	HDMI_task = kthread_create(Hdmi_run_thread, (void *)0, "hdmi proc");
	if (IS_ERR(HDMI_task)) {
		__s32 err = 0;

		__wrn("Unable to start kernel thread %s.\n", "hdmi proc");
		err = PTR_ERR(HDMI_task);
		HDMI_task = NULL;
		return err;
	}
	/* Launch main task loop */
	wake_up_process(HDMI_task);


	audio_func.hdmi_audio_enable = Hdmi_Audio_Enable;
	audio_func.hdmi_set_audio_para = Hdmi_Set_Audio_Para;
	audio_set_hdmi_func(&audio_func);

	disp_func.hdmi_wait_edid = hdmi_wait_edid;
	disp_func.Hdmi_open = Hdmi_open;
	disp_func.Hdmi_close = Hdmi_close;
	disp_func.hdmi_set_mode = Hdmi_set_display_mode;
	disp_func.hdmi_mode_support = Hdmi_mode_support;
	disp_func.hdmi_get_video_timing = hdmi_get_video_timing;
	disp_func.hdmi_get_HPD_status = Hdmi_get_HPD_status;
	disp_func.hdmi_set_pll = Hdmi_set_pll;
	disp_set_hdmi_func(&disp_func);

	return 0;
}

__s32 Hdmi_exit(void)
{

	kfree(run_sem);
	run_sem = NULL;

	if (HDMI_task) {
		kthread_stop(HDMI_task);
		HDMI_task = NULL;
	}

	return 0;
}
