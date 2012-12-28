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

#include <linux/module.h>
#include <mach/aw_ccu.h>
#include "hdmi_core.h"

static char *audio;
module_param(audio, charp, 0444);
MODULE_PARM_DESC(audio,
	"Enable/disable audio over hdmi: "
	"\"EDID:0\": get value from EDID, fallback disabled; "
	"\"EDID:1\": get value from EDID, fallback enabled; "
	"\"0\": disabled; \"1\": enabled.");

__s32 hdmi_state = HDMI_State_Wait_Hpd;
__bool video_enable;
__bool audio_edid;
__bool audio_enable = 1;
__s32 video_mode = HDMI720P_50;
HDMI_AUDIO_INFO audio_info;
__u8 EDID_Buf[1024];
__u8 Device_Support_VIC[512];
static __s32 HPD;

__u32 hdmi_pll = AW_SYS_CLK_PLL3;
__u32 hdmi_clk = 297000000;

struct __disp_video_timing video_timing[] = {
	/* VIC                 PCLK   AVI_PR INPUTX INPUTY HT   HBP  HFP HPSW  VT  VBP VFP VPSW I HSYNC VSYNC */
	{ HDMI1440_480I,       13500000,  1,   720,  480,  858, 119,  19, 62,  525, 18,  4,  3, 1,  0,  0 },
	{ HDMI1440_576I,       13500000,  1,   720,  576,  864, 132,  12, 63,  625, 22,  2,  3, 1,  0,  0 },
	{ HDMI480P,            27000000,  0,   720,  480,  858, 122,  16, 62,  525, 36,  9,  6, 0,  0,  0 },
	{ HDMI576P,            27000000,  0,   720,  576,  864, 132,  12, 64,  625, 44,  5,  5, 0,  0,  0 },
	{ HDMI720P_50,         74250000,  0,  1280,  720, 1980, 260, 440, 40,  750, 25,  5,  5, 0,  1,  1 },
	{ HDMI720P_60,         74250000,  0,  1280,  720, 1650, 260, 110, 40,  750, 25,  5,  5, 0,  1,  1 },
	{ HDMI1080I_50,        74250000,  0,  1920, 1080, 2640, 192, 528, 44, 1125, 20,  2,  5, 1,  1,  1 },
	{ HDMI1080I_60,        74250000,  0,  1920, 1080, 2200, 192,  88, 44, 1125, 20,  2,  5, 1,  1,  1 },
	{ HDMI1080P_50,       148500000,  0,  1920, 1080, 2640, 192, 528, 44, 1125, 41,  4,  5, 0,  1,  1 },
	{ HDMI1080P_60,       148500000,  0,  1920, 1080, 2200, 192,  88, 44, 1125, 41,  4,  5, 0,  1,  1 },
	{ HDMI1080P_24,        74250000,  0,  1920, 1080, 2750, 192, 638, 44, 1125, 41,  4,  5, 0,  1,  1 },
	{ HDMI1080P_24_3D_FP, 148500000,  0,  1920, 2160, 2750, 192, 638, 44, 1125, 41,  4,  5, 0,  1,  1 },
	{ HDMI720P_50_3D_FP,  148500000,  0,  1280, 1440, 1980, 260, 440, 40,  750, 25,  5,  5, 0,  1,  1 },
	{ HDMI720P_60_3D_FP,  148500000,  0,  1280, 1440, 1650, 260, 110, 40,  750, 25,  5,  5, 0,  1,  1 },
	{ HDMI1360_768_60,     85500000,  0,  1360,  768, 1792, 368, 64, 112,  795, 24,  3,  6, 0,  1,  1 },
	{ HDMI1280_1024_60,   108000000,  0,  1280, 1024, 1688, 360, 48, 112, 1066, 41,  1,  3, 0,  1,  1 },
	{ HDMI_EDID, } /* Entry reserved for EDID detected preferred timing */
};

const int video_timing_edid = ARRAY_SIZE(video_timing) - 1;

void hdmi_delay_ms(__u32 t)
{
	__u32 timeout = t * HZ / 1000;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(timeout);
}

__s32 hdmi_core_initial(void)
{
	if (audio) {
		if (strncmp(audio, "EDID:", 5) == 0) {
			audio_edid = 1;
			audio += 5;
		}
		if (strcmp(audio, "0") == 0)
			audio_enable = 0;
	}

	hdmi_state = HDMI_State_Wait_Hpd;
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
	HPD = main_Hpd_Check();
	if (!HPD && hdmi_state > HDMI_State_Wait_Hpd) {
		__inf("plugout\n");
		hdmi_state = HDMI_State_Wait_Hpd;
	}

	/* ? where did all the breaks run off to? --libv */
	switch (hdmi_state) {
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

		if (audio_edid && Device_Support_VIC[HDMI_EDID]) {
			if (audio_info.supported_rates)
				audio_enable = 1;
			else
				audio_enable = 0;
		}
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
		hdmi_state = HDMI_State_Wait_Hpd;
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

__s32 get_video_info(__s32 vic)
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
	__s32 vic_tab;

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

	vic_tab = get_video_info(video_mode);
	if (vic_tab == -1)
		return -1;

	audio_info.CTS = ((video_timing[vic_tab].PCLK / 100) *
			  (audio_info.ACR_N / 128)) / (sample_rate / 100);
	__inf("audio CTS calc:%d\n", audio_info.CTS);

	return 0;
}

__s32 video_config(__s32 vic)
{

	__s32 vic_tab, clk_div, reg_val, dw = 0;

	__inf("video_config, vic:%d\n", vic);

	vic_tab = get_video_info(vic);
	if (vic_tab == -1)
		return 0;
	else
		video_mode = vic;

	if ((vic == HDMI1440_480I) || (vic == HDMI1440_576I))
		dw = 1; /* Double Width */

	HDMI_WUINT32(0x004, 0x00000000);
	HDMI_WUINT32(0x040, 0x00000000); /* disable audio output */
	HDMI_WUINT32(0x010, 0x00000000); /* disable video output */
	/* interrupt mask and clear all interrupt */
	HDMI_WUINT32(0x008, 0xffffffff);

	reg_val = 0x00000000;
	if (dw)
		reg_val |= 0x00000001; /* repetition */
	if (video_timing[vic_tab].I)
		reg_val |= 0x00000010; /* interlace */
	HDMI_WUINT32(0x010, reg_val);

	/* active H */
	HDMI_WUINT16(0x014, (video_timing[vic_tab].INPUTX << dw) - 1);
	/* active HBP */
	HDMI_WUINT16(0x018, (video_timing[vic_tab].HBP << dw) - 1);
	/* active HFP */
	HDMI_WUINT16(0x01c, (video_timing[vic_tab].HFP << dw) - 1);
	/* active HSPW */
	HDMI_WUINT16(0x020, (video_timing[vic_tab].HPSW << dw) - 1);

	/* set active V */
	if ((vic == HDMI1080P_24_3D_FP) || (vic == HDMI720P_50_3D_FP) ||
	    (vic == HDMI720P_60_3D_FP))
		HDMI_WUINT16(0x016, video_timing[vic_tab].INPUTY +
			     video_timing[vic_tab].VBP +
			     video_timing[vic_tab].VFP - 1);
	else if (video_timing[vic_tab].I)
		HDMI_WUINT16(0x016, (video_timing[vic_tab].INPUTY / 2) - 1);
	else
		HDMI_WUINT16(0x016, video_timing[vic_tab].INPUTY - 1);


	HDMI_WUINT16(0x01a, video_timing[vic_tab].VBP - 1); /* active VBP */
	HDMI_WUINT16(0x01e, video_timing[vic_tab].VFP - 1); /* active VFP */
	HDMI_WUINT16(0x022, video_timing[vic_tab].VPSW - 1); /* active VSPW */

	reg_val = 0;
	if (video_timing[vic_tab].HSYNC)
		reg_val |= 0x01; /* Positive Hsync */
	if (video_timing[vic_tab].VSYNC)
		reg_val |= 0x02; /* Positive Vsync */
	HDMI_WUINT16(0x024, reg_val);

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
	HDMI_WUINT8(0x087, (video_timing[vic_tab].VIC >= 256) ?
			   0 : video_timing[vic_tab].VIC);
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

	if (audio_enable)
		HDMI_WUINT8(0x013, 0xc0); /* hdmi mode + hdmi audio */
	else
		HDMI_WUINT8(0x013, 0x80); /* hdmi/dvi mode */
	HDMI_WUINT32(0x004, 0x80000000); /* start hdmi controller */

	if (audio_enable)
		HDMI_WUINT8(0x013, 0xc0); /* hdmi mode + hdmi audio */
	else
		HDMI_WUINT8(0x013, 0x80); /* hdmi/dvi mode */
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

	/*
	 * Below are some notes on the use of register 0x204 and register 0x20c
	 * this is the result of reverse-engineering / trial and error and
	 * thus not necessarily 100% accurate.
	 *
	 * Some notes on register 0x204:
	 * bit 0:     Setting this bit turns the entire fbcon background blue
	 * bit 1:     Setting this bit turns the entire fbcon background green
	 * bit 2:     Setting this bit turns the entire fbcon background red
	 * bit 3-5:   Do not seem to do anything
	 * bit 6:     Selects a pre-scaler for the clock which divides by 2
	 * bit 7-13:  Do not seem to do anything
	 * bit 14-15: Clearing one of these bits causes loss of sync
	 * bit 16-22: Do not seem to do anything
	 * bit 23:    Clearing this bit causes inverse video
	 * bit 24-31: Do not seem to do anything
	 *
	 * Some notes on register 0x020c:
	 * bit 0-15:  Do not seem to do anything
	 * bit 16:    Setting this bit causes the A10 to lockup, do not set!
	 * bit 17-20: Do not seem to do anything
	 * bit 21:    0: PLL3X2 is clocksource 1: PLL7X2 is clocksource
	 * bit 22-23: Setting these bits causes loss of sync, possibly these
	 *            work together with b21 to select a different clocksource
	 * bit 24-31: Do not seem to do anything
	 *
	 * About clocksource selection for the hdmi transmitter:
	 *
	 * It seems that AW_MOD_CLK_HDMI is unused, atleast giving it a
	 * different clock-divider does not cause any problems. Instead the
	 * hdmi transmitter has its own bits to select the sys-clk to use,
	 * with bit 21 of 0x20c selecting between PLL3X2 and PLL7X2, and
	 * bit 6 of 0x204 enabling a pre-scaler dividing by 2, in essence
	 * turning PLL3X2 and PLL7X2 into PLL3 and PLL7. *Or so I believe*,
	 * It could also be that bit 6 of 0x204 simple causes direct selection
	 * of PLL3 and PLL7 through bit 21 of 0x20c, but that seems illogical
	 * since it lives in another register.
	 */

	if (hdmi_pll == AW_SYS_CLK_PLL3 || hdmi_pll == AW_SYS_CLK_PLL7)
		HDMI_WUINT32(0x204, 0x00D8C860);
	else
		HDMI_WUINT32(0x204, 0x00D8C820);

	if (hdmi_pll == AW_SYS_CLK_PLL7 || hdmi_pll == AW_SYS_CLK_PLL7X2)
		HDMI_WUINT32(0x20c, 1 << 21);
	else
		HDMI_WUINT32(0x20c, 0 << 21);

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
