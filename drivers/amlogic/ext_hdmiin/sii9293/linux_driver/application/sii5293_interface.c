
#include "mhl_linuxdrv.h"
#include "../../driver/cra_drv/si_cra.h"

#define GET_VIDEO_INFO_FROM_TABLE

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void sii_set_standby(int bStandby)
{
	SiiRegWrite(RX_A__PD_TOT, !bStandby);
	return ;
}


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

int sii_get_pwr5v_status(void)
{
	char pwr5v;

	pwr5v = SiiRegRead(RX_A__STATE)&RX_M__STATE__PWR5V;

	return (pwr5v==0)?0:1;
}

// audio sampling frequency:
// 0x0 for 44.1 KHz
// 0x1 for Not indicated
// 0x2 for 48 KHz
// 0x3 for 32 KHz
// 0x4 for 22.05 KHz
// 0x6 for 24 kHz
// 0x8 for 88.2 kHz
// 0x9 for 768 kHz (192*4)
// 0xa for 96 kHz
// 0xc for 176.4 kHz
// 0xe for 192 kHz
int sii_get_audio_sampling_freq(void)
{
	unsigned char freq;

	freq = SiiRegRead(RX_A__CHST4)&RX_A__CHST4__BIT_AUD_FS;

	return freq;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifdef GET_VIDEO_INFO_FROM_TABLE
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 output signal horizontal parameters

int sii_get_h_active(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].Active.H;
}

int sii_get_h_total(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].Total.H;
}

int sii_get_hs_width(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].SyncWidth.H;
}

int sii_get_hs_frontporch(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].SyncOffset.H;
}

int sii_get_hs_backporch(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].Blank.H - VideoModeTable[index].SyncOffset.H - VideoModeTable[index].SyncWidth.H;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 output signal vertical parameters

int sii_get_v_active(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].Active.V;
}

int sii_get_v_total(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].Total.V;
}

int sii_get_vs_width(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].SyncWidth.V;
}

int sii_get_vs_frontporch(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].SyncOffset.V;
}

int sii_get_vs_backporch(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].Blank.V - VideoModeTable[index].SyncOffset.V - VideoModeTable[index].SyncWidth.V;
}

int sii_get_vs_to_de(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].Blank.V - VideoModeTable[index].SyncOffset.V;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 output signal clock parameters

int sii_get_pixel_clock(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].PixClk*10000;
}

int sii_get_h_freq(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].HFreq*1000;
}

int sii_get_v_freq(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].VFreq;
}

int sii_get_interlaced(void)
{
	unsigned char index = 0;

	index = gDriverContext.input_video_mode & 0x7f;
	if ( (index==0) || (index>=NMB_OF_CEA861_VIDEO_MODES) )
		return -1;

	return VideoModeTable[index].Interlaced;
}

#else
// !!! read h/v/sync info from 5293 registers, but the value seems not stable.

// offset definitions for registers.
#define MSK__VID_DE_PIXEL_BIT8_11	0x0F
#define MSK__VID_H_RES_BIT8_12		0x1F
#define MSK__VID_HS_WIDTH_BIT8_9	0x03
#define RX_A__VID_HFP2 				0x05a
#define MSK__VID_H_FP_BIT8_9		0x03
#define MSK__VID_DE_LINE_BIT8_10	0x07
#define MSK__VID_V_RES_BIT8_10		0x07
#define MSK__VID_VS_AVT_BIT0_5		0x3F
#define MSK__VID_V_FP_BIT0_5		0x3F

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 output signal horizontal parameters

int sii_get_h_active(void)
{
	unsigned char high,low;

	high = SiiRegRead(RX_A__DE_PIX2)&MSK__VID_DE_PIXEL_BIT8_11;
	low = SiiRegRead(RX_A__DE_PIX1);

	return ( (high<<8) | low );
}

int sii_get_h_total(void)
{
	unsigned char high,low;

	high = SiiRegRead(RX_A__H_RESH)&MSK__VID_H_RES_BIT8_12;
	low = SiiRegRead(RX_A__H_RESL);

	return ( (high<<8) | low );
}

int sii_get_hs_width(void)
{
	unsigned char high,low;

	high = SiiRegRead(RX_A__VID_HAW2)&MSK__VID_HS_WIDTH_BIT8_9;
	low = SiiRegRead(RX_A__VID_HAW1);

	return ( (high<<8) | low );
}

int sii_get_hs_frontporch(void)
{
	unsigned char high,low;

	high = SiiRegRead(RX_A__VID_HFP2)&MSK__VID_H_FP_BIT8_9;
	low = SiiRegRead(RX_A__VID_HFP);

	return ( (high<<8) | low );
}

int sii_get_hs_backporch(void)
{
	int backporch = 0;

	backporch = sii_get_h_total() - sii_get_h_active() - sii_get_hs_frontporch() - sii_get_hs_width();

	return backporch;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 output signal vertical parameters

int sii_get_v_active(void)
{
	unsigned char high,low;

	high = SiiRegRead(RX_A__DE_LINE2)&MSK__VID_DE_LINE_BIT8_10;
	low = SiiRegRead(RX_A__DE_LINE1);

	return ( (high<<8) | low );
}

int sii_get_v_total(void)
{
	unsigned char high,low;

	high = SiiRegRead(RX_A__V_RESH)&MSK__VID_V_RES_BIT8_10;
	low = SiiRegRead(RX_A__V_RESL);

	return ( (high<<8) | low );
}

int sii_get_vs_width(void)
{
	return 0;
}

int sii_get_vs_frontporch(void)
{
	unsigned char low;

	low = SiiRegRead(RX_A__VID_VFP)&MSK__VID_V_FP_BIT0_5;

	return low;
}

int sii_get_vs_backporch(void)
{
	return 0;
}

int sii_get_vs_to_de(void)
{
	unsigned char low;

	low = SiiRegRead(RX_A__VTAVL)&MSK__VID_VS_AVT_BIT0_5;

	return low;
}

#endif

