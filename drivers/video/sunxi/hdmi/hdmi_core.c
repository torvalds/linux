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

#include "hdmi_core.h"

__s32 hdmi_state = HDMI_State_Idle;
__bool video_enable;
__s32 video_mode = HDMI720P_50;
HDMI_AUDIO_INFO audio_info;
__u8 EDID_Buf[1024];
__u8 Device_Support_VIC[512];
static __s32 HPD;

__u32 hdmi_pll;	/* 0:video pll 0; 1:video pll 1 */
__u32 hdmi_clk = 297000000;

static HDMI_VIDE_INFO video_timing[] = {
	/* VIC                 PCLK   AVI_PR INPUTX INPUTY HT  HBP  HFP HPSW  VT  VBP VFP VPSW */
	{HDMI1440_480I,       13500000,  1,   720,  240,  858, 119,  19, 62,  525, 18,  4,  3},
	{HDMI1440_576I,       13500000,  1,   720,  288,  864, 132,  12, 63,  625, 22,  2,  3},
	{HDMI480P,            27000000,  0,   720,  480,  858, 122,  16, 62, 1050, 36,  9,  6},
	{HDMI576P,            27000000,  0,   720,  576,  864, 132,  12, 64, 1250, 44,  5,  5},
	{HDMI720P_50,         74250000,  0,  1280,  720, 1980, 260, 440, 40, 1500, 25,  5,  5},
	{HDMI720P_60,         74250000,  0,  1280,  720, 1650, 260, 110, 40, 1500, 25,  5,  5},
	{HDMI1080I_50,        74250000,  0,  1920,  540, 2640, 192, 528, 44, 1125, 20,  2,  5},
	{HDMI1080I_60,        74250000,  0,  1920,  540, 2200, 192,  88, 44, 1125, 20,  2,  5},
	{HDMI1080P_50,       148500000,  0,  1920, 1080, 2640, 192, 528, 44, 2250, 41,  4,  5},
	{HDMI1080P_60,       148500000,  0,  1920, 1080, 2200, 192,  88, 44, 2250, 41,  4,  5},
	{HDMI1080P_24,        74250000,  0,  1920, 1080, 2750, 192, 638, 44, 2250, 41,  4,  5},
	{HDMI1080P_24_3D_FP, 148500000,  0,  1920, 2160, 2750, 192, 638, 44, 4500, 41,  4,  5},
	{HDMI720P_50_3D_FP,  148500000,  0,  1280, 1440, 1980, 260, 440, 40, 3000, 25,  5,  5},
	{HDMI720P_60_3D_FP,  148500000,  0,  1280, 1440, 1650, 260, 110, 40, 3000, 25,  5,  5},
};

__s32 hdmi_core_initial(void)
{
	hdmi_state = HDMI_State_Idle;
	video_mode = HDMI720P_50;
	memset(&audio_info, 0, sizeof(HDMI_AUDIO_INFO));
	memset(Device_Support_VIC, 0, sizeof(Device_Support_VIC));

	HDMI_WUINT32(0x004, 0x80000000); /* start hdmi controller */

	return 0;
}

static __s32
main_Hpd_Check(void)
{
	__s32 i, times;
	times = 0;

	for (i = 0; i < 3; i++) {
		hdmi_delay_ms(1);
		if (HDMI_RUINT32(0x00c) & 0x01)
			times++;
	}
	if (times == 3)
		return 1;
	else
		return 0;
}

__s32 hdmi_main_task_loop(void)
{
	static __u32 times;

	HPD = main_Hpd_Check();
	if (!HPD) {
		if ((times++) >= 10) {
			times = 0;
			__inf("unplug state\n");
		}

		if (hdmi_state > HDMI_State_Wait_Hpd)
			__inf("plugout\n");

		if (hdmi_state > HDMI_State_Idle)
			hdmi_state = HDMI_State_Wait_Hpd;
	}

	/* ? where did all the breaks run off to? --libv */
	switch (hdmi_state) {
	case HDMI_State_Idle:
		hdmi_state = HDMI_State_Wait_Hpd;
		return 0;

	case HDMI_State_Wait_Hpd:
		if (HPD) {
			hdmi_state = HDMI_State_EDID_Parse;
			__inf("plugin\n");
		} else
			return 0;

	case HDMI_State_Rx_Sense:

	case HDMI_State_EDID_Parse:
		HDMI_WUINT32(0x004, 0x80000000);
		HDMI_WUINT32(0x208, (1 << 31) + (1 << 30) + (1 << 29) +
			     (3 << 27) + (0 << 26) + (1 << 25) + (0 << 24) +
			     (0 << 23) + (4 << 20) + (7 << 17) + (15 << 12) +
			     (7 << 8) + (0x0f << 4) + (8 << 0));
		HDMI_WUINT32(0x200, 0xfe800000); /* txen enable */
		HDMI_WUINT32(0x204, 0x00D8C860); /* ckss = 1 */

		HDMI_WUINT32(0x20c, 0 << 21);

		ParseEDID();
		HDMI_RUINT32(0x5f0);

		hdmi_state = HDMI_State_Wait_Video_config;

	case HDMI_State_Wait_Video_config:
		if (video_enable)
			hdmi_state = HDMI_State_Video_config;
		else
			return 0;

	case HDMI_State_Video_config:
		video_config(video_mode);
		hdmi_state = HDMI_State_Audio_config;

	case HDMI_State_Audio_config:
		audio_config();
		hdmi_state = HDMI_State_Playback;

	case HDMI_State_Playback:
		return 0;

	default:
		__wrn(" unknown hdmi state, set to idle\n");
		hdmi_state = HDMI_State_Idle;
		return 0;
	}
}

__s32 Hpd_Check(void)
{
	if (HPD == 0)
		return 0;
	else if (hdmi_state >= HDMI_State_Wait_Video_config)
		return 1;
	else
		return 0;
}

static __s32 get_video_info(__s32 vic)
{
	__s32 i, count;
	count = sizeof(video_timing);
	for (i = 0; i < count; i++)
		if (vic == video_timing[i].VIC)
			return i;

	__wrn("can't find the video timing parameters\n");
	return -1;
}

static __s32 get_audio_info(__s32 sample_rate)
{
	/*
	 * ACR_N 32000 44100 48000 88200 96000 176400 192000
	 *       4096  6272  6144  12544 12288  25088  24576
	 */
	__inf("sample_rate:%d in get_audio_info\n", sample_rate);

	switch (sample_rate) {
	case 32000:
		audio_info.ACR_N = 4096;
		audio_info.CH_STATUS0 = (3 << 24);
		audio_info.CH_STATUS1 = 0x0000000b;
		break;
	case 44100:
		audio_info.ACR_N = 6272;
		audio_info.CH_STATUS0 = (0 << 24);
		audio_info.CH_STATUS1 = 0x0000000b;
		break;
	case 48000:
		audio_info.ACR_N = 6144;
		audio_info.CH_STATUS0 = (2 << 24);
		audio_info.CH_STATUS1 = 0x0000000b;
		break;
	case 88200:
		audio_info.ACR_N = 12544;
		audio_info.CH_STATUS0 = (8 << 24);
		audio_info.CH_STATUS1 = 0x0000000b;
		break;
	case 96000:
		audio_info.ACR_N = 12288;
		audio_info.CH_STATUS0 = (10 << 24);
		audio_info.CH_STATUS1 = 0x0000000b;
		break;
	case 176400:
		audio_info.ACR_N = 25088;
		audio_info.CH_STATUS0 = (12 << 24);
		audio_info.CH_STATUS1 = 0x0000000b;
		break;
	case 192000:
		audio_info.ACR_N = 24576;
		audio_info.CH_STATUS0 = (14 << 24);
		audio_info.CH_STATUS1 = 0x0000000b;
		break;
	default:
		__wrn("un-support sample_rate,value=%d\n", sample_rate);
		return -1;
	}

	if ((video_mode == HDMI1440_480I) || (video_mode == HDMI1440_576I) ||
	    (video_mode == HDMI480P) || (video_mode == HDMI576P)) {
		audio_info.CTS = ((27000000 / 100) * (audio_info.ACR_N / 128)) /
			(sample_rate / 100);
	} else if ((video_mode == HDMI720P_50) || (video_mode == HDMI720P_60) ||
		   (video_mode == HDMI1080I_50) ||
		   (video_mode == HDMI1080I_60) ||
		   (video_mode == HDMI1080P_24)) {
		audio_info.CTS = ((74250000 / 100) * (audio_info.ACR_N / 128)) /
			(sample_rate / 100);
	} else if ((video_mode == HDMI1080P_50) ||
		   (video_mode == HDMI1080P_60) ||
		   (video_mode == HDMI1080P_24_3D_FP) ||
		   (video_mode == HDMI720P_50_3D_FP) ||
		   (video_mode == HDMI720P_60_3D_FP)) {
		audio_info.CTS = ((148500000 / 100) *
				  (audio_info.ACR_N / 128)) /
			(sample_rate / 100);
	} else {
		__wrn("unkonwn video format when configure audio\n");
		return -1;
	}

	__inf("audio CTS calc:%d\n", audio_info.CTS);

	return 0;
}

__s32 video_config(__s32 vic)
{

	__s32 vic_tab, clk_div, reg_val;

	__inf("video_config, vic:%d\n", vic);

	vic_tab = get_video_info(vic);
	if (vic_tab == -1)
		return 0;
	else
		video_mode = vic;
	HDMI_WUINT32(0x004, 0x00000000);
	HDMI_WUINT32(0x040, 0x00000000); /* disable audio output */
	HDMI_WUINT32(0x010, 0x00000000); /* disable video output */
	/* interrupt mask and clear all interrupt */
	HDMI_WUINT32(0x008, 0xffffffff);

	if ((vic == HDMI1440_480I) || (vic == HDMI1440_576I))
		HDMI_WUINT32(0x010, 0x00000011); /* interlace and repeation */
	else if ((vic == HDMI1080I_50) || (vic == HDMI1080I_60))
		HDMI_WUINT32(0x010, 0x00000010); /* interlace */
	else
		HDMI_WUINT32(0x010, 0x00000000); /* progressive */

	/* need to use repetition? */
	if ((vic == HDMI1440_480I) || (vic == HDMI1440_576I)) {
		/* active H */
		HDMI_WUINT16(0x014, (video_timing[vic_tab].INPUTX << 1) - 1);
		/* active HBP */
		HDMI_WUINT16(0x018, (video_timing[vic_tab].HBP << 1) - 1);
		/* active HFP */
		HDMI_WUINT16(0x01c, (video_timing[vic_tab].HFP << 1) - 1);
		/* active HSPW */
		HDMI_WUINT16(0x020, (video_timing[vic_tab].HPSW << 1) - 1);
	} else {
		/* active H */
		HDMI_WUINT16(0x014, (video_timing[vic_tab].INPUTX << 0) - 1);
		/* active HBP */
		HDMI_WUINT16(0x018, (video_timing[vic_tab].HBP << 0) - 1);
		/* active HFP */
		HDMI_WUINT16(0x01c, (video_timing[vic_tab].HFP << 0) - 1);
		/* active HSPW */
		HDMI_WUINT16(0x020, (video_timing[vic_tab].HPSW << 0) - 1);
	}

	/* set active V */
	if ((vic == HDMI1080P_24_3D_FP) || (vic == HDMI720P_50_3D_FP) ||
	    (vic == HDMI720P_60_3D_FP))
		HDMI_WUINT16(0x016, video_timing[vic_tab].INPUTY +
			     video_timing[vic_tab].VBP +
			     video_timing[vic_tab].VFP - 1);
	else
		HDMI_WUINT16(0x016, video_timing[vic_tab].INPUTY - 1);


	HDMI_WUINT16(0x01a, video_timing[vic_tab].VBP - 1); /* active VBP */
	HDMI_WUINT16(0x01e, video_timing[vic_tab].VFP - 1); /* active VFP */
	HDMI_WUINT16(0x022, video_timing[vic_tab].VPSW - 1); /* active VSPW */

	if (video_timing[vic_tab].PCLK < 74250000) /* SD format */
		HDMI_WUINT16(0x024, 0x00); /* Vsync/Hsync pol */
	else /* HD format */
		HDMI_WUINT16(0x024, 0x03); /* Vsync/Hsync pol */

	HDMI_WUINT16(0x026, 0x03e0); /* TX clock sequence */

	/* avi packet */
	HDMI_WUINT8(0x080, 0x82);
	HDMI_WUINT8(0x081, 0x02);
	HDMI_WUINT8(0x082, 0x0d);
	HDMI_WUINT8(0x083, 0x00);
#ifdef YUV_COLORSPACE /* Fix me */
	/* 4:4:4 YCbCr */
	HDMI_WUINT8(0x084, 0x50); /* Data Byte 1 */
	if (video_timing[vic_tab].PCLK < 74250000) /* 4:3 601 */
		HDMI_WUINT8(0x085, 0x58); /* Data Byte 2 */
	else /* 16:9 709 */
		HDMI_WUINT8(0x085, 0xa8); /* Data Byte 2 */
#else
	/* RGB */
	HDMI_WUINT8(0x084, 0x1E); /* Data Byte 1 */
	/* 4:3 601 */
	HDMI_WUINT8(0x085, 0x58); /* Data Byte 2 */
#endif

	HDMI_WUINT8(0x086, 0x00);
	HDMI_WUINT8(0x087, video_timing[vic_tab].VIC);
	HDMI_WUINT8(0x088, video_timing[vic_tab].AVI_PR);
	HDMI_WUINT8(0x089, 0x00);
	HDMI_WUINT8(0x08a, 0x00);
	HDMI_WUINT8(0x08b, 0x00);
	HDMI_WUINT8(0x08c, 0x00);
	HDMI_WUINT8(0x08d, 0x00);
	HDMI_WUINT8(0x08e, 0x00);
	HDMI_WUINT8(0x08f, 0x00);
	HDMI_WUINT8(0x090, 0x00);

	reg_val = HDMI_RUINT8(0x080) + HDMI_RUINT8(0x081) +
		HDMI_RUINT8(0x082) + HDMI_RUINT8(0x084) +
		HDMI_RUINT8(0x085) + HDMI_RUINT8(0x086) +
		HDMI_RUINT8(0x087) + HDMI_RUINT8(0x088) +
		HDMI_RUINT8(0x089) + HDMI_RUINT8(0x08a) +
		HDMI_RUINT8(0x08b) + HDMI_RUINT8(0x08c) +
		HDMI_RUINT8(0x08d) + HDMI_RUINT8(0x08e) +
		HDMI_RUINT8(0x08f) + HDMI_RUINT8(0x090);
	reg_val = reg_val & 0xff;
	if (reg_val != 0)
		reg_val = 0x100 - reg_val;

	HDMI_WUINT8(0x083, reg_val); /* checksum */

	/* gcp packet */
	HDMI_WUINT32(0x0e0, 0x00000003);
	HDMI_WUINT32(0x0e4, 0x00000000);

	/* vendor infoframe */
	HDMI_WUINT8(0x240, 0x81);
	HDMI_WUINT8(0x241, 0x01);
	HDMI_WUINT8(0x242, 6);	/* length */

	HDMI_WUINT8(0x243, 0x29); /* pb0:checksum */
	HDMI_WUINT8(0x244, 0x03); /* pb1-3:24bit ieee id */
	HDMI_WUINT8(0x245, 0x0c);
	HDMI_WUINT8(0x246, 0x00);
	HDMI_WUINT8(0x247, 0x40); /* pb4 */
	HDMI_WUINT8(0x248, 0x00); /* pb5:3d meta not present, frame packing */

	HDMI_WUINT8(0x249, 0x00); /* pb6:extra data for 3d */
	HDMI_WUINT8(0x24a, 0x00); /* pb7: matadata type=0,len=8 */
	HDMI_WUINT8(0x24b, 0x00);
	HDMI_WUINT8(0x24c, 0x00);
	HDMI_WUINT8(0x24d, 0x00);
	HDMI_WUINT8(0x24e, 0x00);
	HDMI_WUINT8(0x24f, 0x00);
	HDMI_WUINT8(0x250, 0x00);
	HDMI_WUINT8(0x251, 0x00);
	HDMI_WUINT8(0x252, 0x00);

	/* packet config */
	if ((vic != HDMI1080P_24_3D_FP) && (vic != HDMI720P_50_3D_FP) &&
	    (vic != HDMI720P_60_3D_FP)) {
		HDMI_WUINT32(0x2f0, 0x0000f321);
		HDMI_WUINT32(0x2f4, 0x0000000f);
	} else {
		HDMI_WUINT32(0x2f0, 0x00005321);
		HDMI_WUINT32(0x2f4, 0x0000000f);
	}

	HDMI_WUINT32(0x300, 0x08000000); /* set input sync enable */

	HDMI_WUINT8(0x013, 0xc0); /* hdmi mode */
	HDMI_WUINT32(0x004, 0x80000000); /* start hdmi controller */

	HDMI_WUINT8(0x013, 0xc0); /* hdmi mode */
	HDMI_WUINT32(0x004, 0x80000000); /* start hdmi controller */

	/* hdmi pll setting */
	if ((vic == HDMI1440_480I) || (vic == HDMI1440_576I)) {
		clk_div = hdmi_clk / video_timing[vic_tab].PCLK;
		clk_div /= 2;
	} else {
		clk_div = hdmi_clk / video_timing[vic_tab].PCLK;
	}
	clk_div &= 0x0f;

	HDMI_WUINT32(0x208,
		     (1 << 31) + (1 << 30) + (1 << 29) + (3 << 27) + (0 << 26) +
		     (1 << 25) + (0 << 24) + (0 << 23) + (4 << 20) + (7 << 17) +
		     (15 << 12) + (7 << 8) + (clk_div << 4) + (8 << 0));

	/* tx driver setting */
	HDMI_WUINT32(0x200, 0xfe800000); /* txen enable */
	HDMI_WUINT32(0x204, 0x00D8C860); /* ckss = 1 */

	HDMI_WUINT32(0x20c, hdmi_pll << 21);

	return 0;
}

__s32 audio_config(void)
{
	__s32 i;

	__inf("audio_config, sample_rate:%d\n", audio_info.sample_rate);

	HDMI_WUINT32(0x040, 0x00000000);
	HDMI_WUINT32(0x040, 0x40000000);
	while (HDMI_RUINT32(0x040) != 0)
		;
	HDMI_WUINT32(0x040, 0x40000000);
	while (HDMI_RUINT32(0x040) != 0)
		;

	if (!audio_info.audio_en)
		return 0;

	i = get_audio_info(audio_info.sample_rate);
	if (i == -1)
		return 0;

	if (audio_info.channel_num == 1) {
		/* audio fifo rst and select ddma, 2 ch 16bit pcm */
		HDMI_WUINT32(0x044, 0x00000000);
		HDMI_WUINT32(0x048, 0x00000000); /* ddma,pcm layout0 1ch */
		HDMI_WUINT32(0x04c, 0x76543200);

		HDMI_WUINT32(0x0A0, 0x710a0184); /* audio infoframe head */
		HDMI_WUINT32(0x0A4, 0x00000000); /* CA = 0X1F */
		HDMI_WUINT32(0x0A8, 0x00000000);
		HDMI_WUINT32(0x0Ac, 0x00000000);
	} else if (audio_info.channel_num == 2) {
		/* audio fifo rst and select ddma, 2 ch 16bit pcm */
		HDMI_WUINT32(0x044, 0x00000000);
		HDMI_WUINT32(0x048, 0x00000001); /* ddma,pcm layout0 2ch */
		HDMI_WUINT32(0x04c, 0x76543210);

		HDMI_WUINT32(0x0A0, 0x710a0184); /* audio infoframe head */
		HDMI_WUINT32(0x0A4, 0x00000000); /* CA = 0X1F */
		HDMI_WUINT32(0x0A8, 0x00000000);
		HDMI_WUINT32(0x0Ac, 0x00000000);
	} else if (audio_info.channel_num == 8) {
		/* audio fifo rst and select ddma, 2 ch 16bit pcm */
		HDMI_WUINT32(0x044, 0x00000000);
		HDMI_WUINT32(0x048, 0x0000000f); /* ddma,pcm layout1 8ch */
		HDMI_WUINT32(0x04c, 0x76543210);

		HDMI_WUINT32(0x0A0, 0x520a0184); /* audio infoframe head */
		HDMI_WUINT32(0x0A4, 0x1F000000); /* CA = 0X1F */
		HDMI_WUINT32(0x0A8, 0x00000000);
		HDMI_WUINT32(0x0Ac, 0x00000000);
	} else
		__wrn("unkonwn num_ch:%d\n", audio_info.channel_num);

	HDMI_WUINT32(0x050, audio_info.CTS); /* CTS and N */
	HDMI_WUINT32(0x054, audio_info.ACR_N);
	HDMI_WUINT32(0x058, audio_info.CH_STATUS0);
	HDMI_WUINT32(0x05c, audio_info.CH_STATUS1);

	HDMI_WUINT32(0x040, 0x80000000);
	HDMI_WUINT32(0x004, 0x80000000);

	/* for audio test */
#if 0
	/* dedicated dma setting aw1623 env */
	/* ddma ch5 seting from addr =0x40c00000 */
	sys_put_wvalue(0xf1c023a4, 0x40c00000);
	sys_put_wvalue(0xf1c023a8, 0x00000000); /* des =0 */
	sys_put_wvalue(0xf1c023ac, 0x01f00000); /* byte to trans */
	sys_put_wvalue(0xf1c023b8, (31 << 24) + (7 << 16) + (31 << 8) +
		       (7 << 0)); /* data block and wait cycle */
	/* from src0 to des1,continous mode */
	sys_put_wvalue(0xf1c023a0, 0xa4b80481);
#endif

	return 0;
}
