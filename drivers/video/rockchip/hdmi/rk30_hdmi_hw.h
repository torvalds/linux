#ifndef __RK30_HDMI_HW_H__
#define __RK30_HDMI_HW_H__

/* HDMI_SYS_CONTROL */
#define SYS_CTRL	0x0

enum {
	PWR_SAVE_MODE_A = 1,
	PWR_SAVE_MODE_B = 2,
	PWR_SAVE_MODE_D = 4,
	PWR_SAVE_MODE_E = 8
};
#define m_PWR_SAVE_MODE		0xF0
#define v_PWR_SAVE_MODE(n)	(n << 4)
#define PLL_B_RESET			(1 << 3)

#define N_32K 		0x1000
#define N_441K 		0x1880
#define N_882K 		0x3100
#define N_1764K 	0x6200
#define N_48K 		0x1800
#define N_96K		0x3000
#define N_192K 		0x6000

#define LR_SWAP_N3			0x04
#define N_2					0x08
#define N_1					0x0c

#define AUDIO_CTRL1			0x28
#define AUDIO_CTRL2 		0x2c
#define I2S_AUDIO_CTRL		0x30
enum {
	I2S_MODE_STANDARD = 0,
	I2S_MODE_RIGHT_JUSTIFIED,
	I2S_MODE_LEFT_JUSTIFIED
};
#define v_I2S_MODE(n)		n
enum {
	I2S_CHANNEL_1_2 = 1,
	I2S_CHANNEL_3_4 = 3,
	I2S_CHANNEL_5_6 = 7,
	I2S_CHANNEL_7_8 = 0xf
};
#define v_I2S_CHANNEL(n)	( (n) << 2 )

#define I2S_INPUT_SWAP		0x40

#define SRC_NUM_AUDIO_LEN	0x50

/* HDMI_AV_CTRL1*/
#define AV_CTRL1	0x54
enum {
	AUDIO_32K	= 0x3,
	AUDIO_441K	= 0x0,
	AUDIO_48K	= 0x2,
	AUDIO_882K	= 0x8,
	AUDIO_96K	= 0xa,
	AUDIO_1764K	= 0xc,
	AUDIO_192K	= 0xe,
};
#define m_AUDIO_SAMPLE_RATE		0xF0
#define v_AUDIO_SAMPLE_RATE(n)	(n << 4)
enum {
	VIDEO_INPUT_RGB_YCBCR_444 = 0,
	VIDEO_INPUT_YCBCR422,
	VIDEO_INPUT_YCBCR422_EMBEDDED_SYNC,
	VIDEO_INPUT_2X_CLOCK,
	VIDEO_INPUT_2X_CLOCK_EMBEDDED_SYNC,
	VIDEO_INPUT_RGB444_DDR,
	VIDEO_INPUT_YCBCR422_DDR
};
#define m_INPUT_VIDEO_MODE			(7 << 1)
#define v_INPUT_VIDEO_MODE(n)		(n << 1)
enum {
	INTERNAL_DE = 0,
	EXTERNAL_DE
};
#define m_DE_SIGNAL_SELECT			(1 << 0)

/* HDMI_AV_CTRL2 */
#define AV_CTRL2	0xec
#define m_CSC_ENABLE				(1 << 0)
#define v_CSC_ENABLE(n)				(n)

/* HDMI_VIDEO_CTRL1 */
#define VIDEO_CTRL1	0x58
enum {
	VIDEO_OUTPUT_RGB444 = 0,
	VIDEO_OUTPUT_YCBCR444,
	VIDEO_OUTPUT_YCBCR422
};
#define m_VIDEO_OUTPUT_MODE		(0x3 << 6)
#define v_VIDEO_OUTPUT_MODE(n)	(n << 6)
enum {
	VIDEO_INPUT_DEPTH_12BIT = 0,
	VIDEO_INPUT_DEPTH_10BIT = 0x1,
	VIDEO_INPUT_DEPTH_8BIT = 0x3
};
#define m_VIDEO_INPUT_DEPTH		(3 << 4)
#define v_VIDEO_INPUT_DEPTH(n)	(n << 4)
enum {
	VIDEO_EMBEDDED_SYNC_LOCATION_0 = 0,
	VIDEO_EMBEDDED_SYNC_LOCATION_1,
	VIDEO_EMBEDDED_SYNC_LOCATION_2
};
#define m_VIDEO_EMBEDDED_SYNC_LOCATION		(3 << 2)
#define VIDEO_EMBEDDED_SYNC_LOCATION(n)		(n << 2)
enum {
	VIDEO_INPUT_COLOR_RGB = 0,
	VIDEO_INPUT_COLOR_YCBCR
};
#define m_VIDEO_INPUT_COLOR_MODE			(1 << 0)

/* DEEP_COLOR_MODE */
#define DEEP_COLOR_MODE	0x5c
enum{
	TMDS_CLOCK_MODE_8BIT = 0,
	TMDS_CLOKK_MODE_10BIT,
	TMDS_CLOKK_MODE_12BIT
};
#define TMDS_CLOCK_MODE_MASK	0x3 << 6
#define TMDS_CLOCK_MODE(n)		(n) << 6

#define CSC_PARA_C0_H	0x60
#define CSC_PARA_C0_L	0x64
#define CSC_PARA_C1_H	0x68
#define CSC_PARA_C1_L	0x6c
#define CSC_PARA_C2_H	0x70
#define CSC_PARA_C2_L	0x74
#define CSC_PARA_C3_H	0x78
#define CSC_PARA_C3_L	0x7c
#define CSC_PARA_C4_H	0x80
#define CSC_PARA_C4_L	0x84
#define CSC_PARA_C5_H	0x88
#define CSC_PARA_C5_L	0x8c
#define CSC_PARA_C6_H	0x90
#define CSC_PARA_C6_L	0x94
#define CSC_PARA_C7_H	0x98
#define CSC_PARA_C7_L	0x9c
#define CSC_PARA_C8_H	0xa0
#define CSC_PARA_C8_L	0xa4
#define CSC_PARA_C9_H	0xa8
#define CSC_PARA_C9_L	0xac
#define CSC_PARA_C10_H	0xac
#define CSC_PARA_C10_L	0xb4
#define CSC_PARA_C11_H	0xb8
#define CSC_PARA_C11_L	0xbc

#define CSC_CONFIG1		0x34c
#define m_CSC_MODE			(1 << 7)
#define m_CSC_COEF_MODE 	(0xF << 3)	//Only used in auto csc mode
#define m_CSC_STATUS		(1 << 2)
#define m_CSC_VID_SELECT	(1 << 1)
#define m_CSC_BRSWAP_DIABLE	(1)

enum {
	CSC_MODE_MANUAL	= 0,
	CSC_MODE_AUTO
};
#define v_CSC_MODE(n)			(n << 7)
enum {
	COE_SDTV_LIMITED_RANGE = 0x08,
	COE_SDTV_FULL_RANGE = 0x04,
	COE_HDTV_60Hz = 0x2,
	COE_HDTV_50Hz = 0x1
};
#define v_CSC_COE_MODE(n)		(n << 3)
enum {
	CSC_INPUT_VID_5_19 = 0,
	CSC_INPUT_VID_28_29
};
#define v_CSC_VID_SELECT(n)		(n << 1)
#define v_CSC_BRSWAP_DIABLE(n)	(n)

/* VIDEO_SETTING2 */
#define VIDEO_SETTING2	0x114
#define m_UNMUTE					(1 << 7)
#define m_MUTE						(1 << 6)
#define m_AUDIO_RESET				(1 << 2)
#define m_NOT_SEND_AUDIO			(1 << 1)
#define m_NOT_SEND_VIDEO			(1 << 0)
#define AV_UNMUTE					(1 << 7)		// Unmute video and audio, send normal video and audio data
#define AV_MUTE						(1 << 6)		// Mute video and audio, send black video data and silent audio data
#define AUDIO_CAPTURE_RESET			(1 << 2)		// Reset audio process logic, only available in pwr_e mode.
#define NOT_SEND_AUDIO				(1 << 1)		// Send silent audio data
#define NOT_SEND_VIDEO				(1 << 0)		// Send black video data

/* CONTROL_PACKET_BUF_INDEX */
#define CONTROL_PACKET_BUF_INDEX	0x17c
enum {
	INFOFRAME_AVI = 0x06,
	INFOFRAME_AAI = 0x08
};
#define CONTROL_PACKET_HB0			0x180
#define CONTROL_PACKET_HB1			0x184
#define CONTROL_PACKET_HB2			0x188
#define CONTROL_PACKET_PB_ADDR		0x18c
#define SIZE_AVI_INFOFRAME			0x11	// 17 bytes
#define SIZE_AUDIO_INFOFRAME		0x0F	// 15 bytes
enum {
	AVI_COLOR_MODE_RGB = 0,
	AVI_COLOR_MODE_YCBCR422,
	AVI_COLOR_MODE_YCBCR444
};
enum {
	AVI_COLORIMETRY_NO_DATA = 0,
	AVI_COLORIMETRY_SMPTE_170M,
	AVI_COLORIMETRY_ITU709,
	AVI_COLORIMETRY_EXTENDED
};
enum {
	AVI_CODED_FRAME_ASPECT_NO_DATA,
	AVI_CODED_FRAME_ASPECT_4_3,
	AVI_CODED_FRAME_ASPECT_16_9
};
enum {
	ACTIVE_ASPECT_RATE_SAME_AS_CODED_FRAME = 0x08,
	ACTIVE_ASPECT_RATE_4_3,
	ACTIVE_ASPECT_RATE_16_9,
	ACTIVE_ASPECT_RATE_14_9
};


/* HDCP_CTRL */
#define HDCP_CTRL		0x2bc

enum {
	OUTPUT_DVI = 0,
	OUTPUT_HDMI
};
#define m_HDMI_DVI		(1 << 1)
#define v_HDMI_DVI(n)	(n << 1)

#define EXT_VIDEO_PARA			0xC0
#define m_VSYNC_OFFSET			(0xF << 4)
#define m_VSYNC_POLARITY		(1 << 3)
#define m_HSYNC_POLARITY		(1 << 2)
#define m_INTERLACE				(1 << 1)
#define m_EXT_VIDEO_ENABLE		(1 << 0)

#define v_VSYNC_OFFSET(n)		(n << 4)
#define v_VSYNC_POLARITY(n)		(n << 3)
#define v_HSYNC_POLARITY(n)		(n << 2)
#define v_INTERLACE(n)			(n << 1)
#define v_EXT_VIDEO_ENABLE(n)	(n << 0) 

#define EXT_VIDEO_PARA_HTOTAL_L		0xC4
#define EXT_VIDEO_PARA_HTOTAL_H		0xC8
#define EXT_VIDEO_PARA_HBLANK_L		0xCC
#define EXT_VIDEO_PARA_HBLANK_H		0xD0
#define EXT_VIDEO_PARA_HDELAY_L		0xD4
#define EXT_VIDEO_PARA_HDELAY_H		0xD8
#define EXT_VIDEO_PARA_HSYNCWIDTH_L	0xDC
#define EXT_VIDEO_PARA_HSYNCWIDTH_H	0xE0

#define EXT_VIDEO_PARA_VTOTAL_L		0xE4
#define EXT_VIDEO_PARA_VTOTAL_H		0xE8
#define EXT_VIDEO_PARA_VBLANK_L		0xF4
#define EXT_VIDEO_PARA_VDELAY		0xF8
#define EXT_VIDEO_PARA_VSYNCWIDTH	0xFC

#define PHY_PLL_SPEED				0x158
	#define v_TEST_EN(n)			(n << 6)
	#define v_PLLA_BYPASS(n)		(n << 4)
	#define v_PLLB_SPEED(n)			(n << 2)
	#define v_PLLA_SPEED(n)			(n)
	enum {
		PLL_SPEED_LOWEST = 0,
		PLL_SPEED_MIDLOW,
		PLL_SPEED_MIDHIGH,
		PLL_SPEED_HIGHEST
	};

#define PHY_PLL_17					0x15c		// PLL A & B config bit 17
	#define v_PLLA_BIT17(n)			(n << 2)
	#define v_PLLB_BIT17(n)			(n << 1)
	
#define PHY_BGR						0x160
	#define v_BGR_DISCONNECT(n)		(n << 7)
	#define v_BGR_V_OFFSET(n)		(n << 4)
	#define v_BGR_I_OFFSET(n)		(n)

#define PHY_PLLA_1					0x164
#define PHY_PLLA_2					0x168
#define PHY_PLLB_1					0x16c
#define PHY_PLLB_2					0x170

#define PHY_DRIVER_PREEMPHASIS		0x174
	#define v_TMDS_SWING(n)			(n << 4)
	#define v_PRE_EMPHASIS(n)		(n)
	
#define PHY_PLL_16_AML				0x178		// PLL A & B config bit 16 and AML control
	#define v_PLLA_BIT16(n)			(n << 5)
	#define v_PLLB_BIT16(n)			(n << 4)
	#define v_AML(n)				(n)

#define INTR_MASK1					0x248
#define INTR_MASK2					0x24c
#define INTR_MASK3					0x258
#define INTR_MASK4					0x25c
#define INTR_STATUS1				0x250
#define INTR_STATUS2				0x254
#define INTR_STATUS3				0x260
#define INTR_STATUS4				0x264

#define m_INT_HOTPLUG				(1 << 7)
#define m_INT_MSENS					(1 << 6)
#define m_INT_VSYNC					(1 << 5)
#define m_INT_AUDIO_FIFO_FULL		(1 << 4)
#define m_INT_EDID_READY			(1 << 2)
#define m_INT_EDID_ERR				(1 << 1)

#define DDC_READ_FIFO_ADDR			0x200
#define DDC_BUS_FREQ_L				0x204
#define DDC_BUS_FREQ_H				0x208
#define DDC_BUS_CTRL				0x2dc
#define DDC_I2C_LEN					0x278
#define DDC_I2C_OFFSET				0x280
#define DDC_I2C_CTRL				0x284
#define DDC_I2C_READ_BUF0			0x288
#define DDC_I2C_READ_BUF1			0x28c
#define DDC_I2C_READ_BUF2			0x290
#define DDC_I2C_READ_BUF3			0x294
#define DDC_I2C_WRITE_BUF0			0x298
#define DDC_I2C_WRITE_BUF1			0x29c
#define DDC_I2C_WRITE_BUF2			0x2a0
#define DDC_I2C_WRITE_BUF3			0x2a4
#define DDC_I2C_WRITE_BUF4			0x2ac
#define DDC_I2C_WRITE_BUF5			0x2b0
#define DDC_I2C_WRITE_BUF6			0x2b4

#define EDID_SEGMENT_POINTER		0x310
#define EDID_WORD_ADDR				0x314
#define EDID_FIFO_ADDR				0x318

#define HPD_MENS_STA				0x37c
#define m_HOTPLUG_STATUS			(1 << 7)
#define m_MSEN_STATUS				(1 << 6)


#define HDMIRdReg(addr)						__raw_readl(hdmi->regbase + addr)
#define HDMIWrReg(addr, val)        		__raw_writel((val), hdmi->regbase + addr);
#define HDMIMskReg(temp, addr, msk, val)	\
	temp = __raw_readl(hdmi->regbase + addr) & (0xFF - (msk)) ; \
	__raw_writel(temp | ( (val) & (msk) ),  hdmi->regbase + addr); 

/* RK30 HDMI Video Configure Parameters */
struct rk30_hdmi_video_para {
	int vic;
	int input_mode;		//input video data interface
	int input_color;	//input video color mode
	int output_mode;	//output hdmi or dvi
	int output_color;	//output video color mode
};

/* Color Space Convertion Mode */
enum {
	CSC_RGB_0_255_TO_ITU601_16_235 = 0,	//RGB 0-255 input to YCbCr 16-235 output according BT601
	CSC_RGB_0_255_TO_ITU709_16_235,		//RGB 0-255 input to YCbCr 16-235 output accroding BT709
	CSC_ITU601_16_235_TO_RGB_16_235,	//YCbCr 16-235 input to RGB 16-235 output according BT601
	CSC_ITU709_16_235_TO_RGB_16_235,	//YCbCr 16-235 input to RGB 16-235 output according BT709
	CSC_ITU601_16_235_TO_RGB_0_255,		//YCbCr 16-235 input to RGB 0-255 output according BT601
	CSC_ITU709_16_235_TO_RGB_0_255		//YCbCr 16-235 input to RGB 0-255 output according BT709
};

extern int rk30_hdmi_detect_hotplug(void);
extern int rk30_hdmi_read_edid(int block, unsigned char *buff);
extern int rk30_hdmi_removed(void);
extern int rk30_hdmi_config_video(struct rk30_hdmi_video_para *vpara);
extern int rk30_hdmi_config_audio(struct hdmi_audio *audio);
extern void rk30_hdmi_control_output(int enable);
#endif