
#include <local_types.h>
#include <amf.h>
#include <registers.h>
#include "sii9233_drv.h"
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/delay.h>

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a hardware power reset

void sii_hardware_reset(sii9233a_info_t *info)
{
	amlogic_gpio_direction_output(info->gpio_reset, 0, SII9233A_DRV_NAME);
    msleep(60);
    amlogic_gpio_direction_output(info->gpio_reset, 1, SII9233A_DRV_NAME);
}

int sii_get_pwr5v_status(void)
{
	char pwr5v;

	pwr5v = RegisterRead(REG__STATE)&BIT__PWR5V;

	return (pwr5v==0)?0:1;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a input hdmi port

char sii_get_hdmi_port(void)
{
	char port;

	port = RegisterRead(REG__PORT_SWTCH2);

	return (port&MSK__PORT_EN);
}

void sii_set_hdmi_port(char port)
{
	if( (port>=0) && (port<4) )
	{
		RegisterWrite(REG__PORT_SWTCH, (1<<(port+4)) );
		RegisterModify(REG__PORT_SWTCH2, MSK__PORT_EN, port);
	}

	return ;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a output signal horizontal parameters

int sii_get_h_active(void)
{
	unsigned char high,low;

	high = RegisterRead(REG__VID_DE_PIXEL2)&MSK__VID_DE_PIXEL_BIT8_11;
	low = RegisterRead(REG__VID_DE_PIXEL1);

	return ( (high<<8) | low );
}

int sii_get_h_total(void)
{
	unsigned char high,low;

	high = RegisterRead(REG__VID_H_RES2)&MSK__VID_H_RES_BIT8_12;
	low = RegisterRead(REG__VID_H_RES1);

	return ( (high<<8) | low );
}

int sii_get_hs_width(void)
{
	unsigned char high,low;

	high = RegisterRead(REG__VID_HS_WIDTH2)&MSK__VID_HS_WIDTH_BIT8_9;
	low = RegisterRead(REG__VID_HS_WIDTH1);

	return ( (high<<8) | low );
}

int sii_get_hs_frontporch(void)
{
	unsigned char high,low;

	high = RegisterRead(REG__VID_H_FP2)&MSK__VID_H_FP_BIT8_9;
	low = RegisterRead(REG__VID_H_FP1);

	return ( (high<<8) | low );
}

int sii_get_hs_backporch(void)
{
	int backporch = 0;

	backporch = sii_get_h_total() - sii_get_h_active() - sii_get_hs_frontporch() - sii_get_hs_width();

	return backporch;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a output signal vertical parameters

int sii_get_v_active(void)
{
	unsigned char high,low;

	high = RegisterRead(REG__VID_DE_LINE2)&MSK__VID_DE_LINE_BIT8_10;
	low = RegisterRead(REG__VID_DE_LINE1);

	return ( (high<<8) | low );
}

int sii_get_v_total(void)
{
	unsigned char high,low;

	high = RegisterRead(REG__VID_V_RES2)&MSK__VID_V_RES_BIT8_10;
	low = RegisterRead(REG__VID_V_RES1);

	return ( (high<<8) | low );
}

int sii_get_vs_width(void)
{
	return 0;
}

int sii_get_vs_to_de(void)
{
	unsigned char low;

	low = RegisterRead(REG__VID_VS_AVT)&MSK__VID_VS_AVT_BIT0_5;

	return low;
}

int sii_get_vs_frontporch(void)
{
	unsigned char low;

	low = RegisterRead(REG__VID_V_FP)&MSK__VID_V_FP_BIT0_5;

	return low;
}

int sii_get_vs_backporch(void)
{
	return 0;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a video pixel clock
#if 0
// this 2 parameters seem not correct according to 9233 program guide document.

int sii_get_video_pixel_clock(void)
{
	unsigned char high,low;

	high = RegisterRead(REG__VIDA_XPCNT1)&MSK__VIDA_XPCNT1_BIT8_11;
	low = RegisterRead(REG__VIDA_XPCNT0);

	return ( (high<<8) | low );
}

int sii_get_frame_rate(void)
{
	unsigned int h_total, v_total, f_xtal = 27000000, clk;
	unsigned int t_framerate;

	h_total = sii_get_h_total();
	v_total = sii_get_v_total();
	clk = sii_get_video_pixel_clock();

	t_framerate = h_total*clk*v_total/2048/f_xtal;

	return (int)t_framerate;
}
#endif
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a utility
void dump_input_video_info(void)
{
	int height,width,h_total,v_total;
	int hs_fp,hs_width,hs_bp;
	int vs_fp,vs_de;
	//int clk,frame_rate;

	height = sii_get_h_active();
	width = sii_get_v_active();

	h_total = sii_get_h_total();
	v_total = sii_get_v_total();

	hs_fp = sii_get_hs_frontporch();
	hs_width = sii_get_hs_width();
	hs_bp = sii_get_hs_backporch();

	vs_fp = sii_get_vs_frontporch();
	vs_de = sii_get_vs_to_de();
	//clk = sii_get_video_pixel_clock();
	//frame_rate = sii_get_frame_rate();

	printk("sii9223a hdmi-in video info:\n\n\
		height * width = %4d x %4d, ( %4d x %4d )\n\
		h sync = %4d, %4d, %4d\n\
		v sync = %4d,      %4d\n",
		height,width,h_total,v_total,
		hs_fp,hs_width,hs_bp,
		vs_fp,vs_de);
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a output spdif status
int sii_get_spdif_status(void)
{
	//todo
	return 0;
}

int sii_is_hdmi_mode(void)
{
	unsigned char mode;

	mode = RegisterRead(REG__AUDP_STAT)&BIT__HDMI_DET;
	mode = (mode==0)?0:1;

	return mode;
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

	freq = RegisterRead(REG__AUD_CHST4)&BIT__AUD_FS;

	return freq;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a general status
int sii_get_chip_id(void)
{
	unsigned char high, low;

	high = RegisterRead(REG__IDH_RX);
	low = RegisterRead(REG__IDL_RX);

	return ( (high<<8) | low );
}
