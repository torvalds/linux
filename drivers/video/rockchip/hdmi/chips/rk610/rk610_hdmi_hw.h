#ifndef _RK610_HDMI_HW_H
#define _RK610_HDMI_HW_H

enum {
		OUTPUT_DVI = 0,
		OUTPUT_HDMI
	};
	
#define SYS_CTRL					0x00
	#define m_INT_POL				(1 << 0)
	#define m_POWER					(1 << 1)
	#define m_REG_CLK_SOURCE		(1 << 2)
	#define v_INT_POL_HIGH			1
	#define v_INT_POL_LOW			0
	#define v_PWR_ON				(0 << 1)
	#define v_PWR_OFF				(1 << 1)
	#define v_REG_CLK_SOURCE_TMDS	(0 << 2)
	#define v_REG_CLK_SOURCE_IIS	(1 << 2)
#define VIDEO_CONTRL1				0x01
	#define m_VIDEO_INPUT_FORMAT	(7 << 1)
	#define m_DE_SOURCE				(1 << 0)
	enum {
		VIDEO_INPUT_SDR_RGB444 = 0,
		VIDEO_INPUT_DDR_RGB444 = 5,
		VIDEO_INPUT_DDR_YCBCR422 = 6			
	};
	#define v_VIDEO_INPUT_FORMAT(n)	(n << 1)
	#define v_DE_EXTERNAL			1
	#define v_DE_INTERANL			0
	
#define VIDEO_CONTRL2				0x02
	#define m_VIDEO_OUTPUT_FORMAT	(3 << 6)
	#define m_VIDEO_INPUT_BITS		(3 << 4)
	#define v_VIDEO_OUTPUT_FORMAT(n)(n << 6)
	#define v_VIDEO_INPUT_BITS(n)	(n << 4)
	enum{
		VIDEO_INPUT_12BITS = 0,
		VIDEO_INPUT_10BITS,
		VIDEO_INPUT_8BITS
	};
#define VIDEO_CONTRL3				0x04
	#define m_SOF					(1 << 3)
	#define m_CSC					(1 << 0)
	#define v_SOF_ENABLE			(0 << 3)
	#define v_SOF_DISABLE			(1 << 3)
	#define v_CSC_ENABLE			1
	#define v_CSC_DISABLE			0
	
#define AV_MUTE						0x05
	#define m_AVMUTE_CLEAR			(1 << 7)
	#define m_AVMUTE_ENABLE			(1 << 6)
	#define m_AUDIO_MUTE			(1 << 1)
	#define m_VIDEO_BLACK			(1 << 0)
	#define v_AUDIO_MUTE(n)			(n << 1)
	#define v_VIDEO_MUTE(n)			(n << 0)
	
#define VIDEO_TIMING_CTL			0x08
	#define v_HSYNC_POLARITY(n)		(n << 3)
	#define v_VSYNC_POLARITY(n)		(n << 2)
	#define v_INETLACE(n)			(n << 1)
	#define v_EXTERANL_VIDEO(n)		(n << 0)
	
#define VIDEO_EXT_HTOTAL_L			0x09
#define VIDEO_EXT_HTOTAL_H			0x0a
#define VIDEO_EXT_HBLANK_L			0x0b
#define VIDEO_EXT_HBLANK_H			0x0c
#define VIDEO_EXT_HDELAY_L			0x0d
#define VIDEO_EXT_HDELAY_H			0x0e
#define VIDEO_EXT_HDURATION_L		0x0f
#define VIDEO_EXT_HDURATION_H		0x10
#define VIDEO_EXT_VTOTAL_L			0x11
#define VIDEO_EXT_VTOTAL_H			0x12
#define VIDEO_EXT_VBLANK			0x13
#define VIDEO_EXT_VDELAY			0x14
#define VIDEO_EXT_VDURATION			0x15

#define AUDIO_CTRL1					0x35
	enum {
		CTS_SOURCE_INTERNAL = 0,
		CTS_SOURCE_EXTERNAL
	};
	#define v_CTS_SOURCE(n)			(n << 7)
	enum {
		DOWNSAMPLE_DISABLE = 0,
		DOWNSAMPLE_1_2,
		DOWNSAMPLE_1_4
	};
	#define v_DOWN_SAMPLE(n)		(n << 5)
	enum {
		AUDIO_SOURCE_IIS = 0,
		AUDIO_SOURCE_SPDIF
	};
	#define v_AUDIO_SOURCE(n)		(n << 3)
	#define v_MCLK_ENABLE(n)		(n << 2)
	enum {
		MCLK_128FS = 0,
		MCLK_256FS,
		MCLK_384FS,
		MCLK_512FS
	};
	#define v_MCLK_RATIO(n)			(n)
	
#define AUDIO_SAMPLE_RATE			0x37
	enum {
		AUDIO_32K	= 0x3,
		AUDIO_441K	= 0x0,
		AUDIO_48K	= 0x2,
		AUDIO_882K	= 0x8,
		AUDIO_96K	= 0xa,
		AUDIO_1764K	= 0xc,
		AUDIO_192K	= 0xe,
	};

#define AUDIO_I2S_MODE				0x38
	enum {
		I2S_CHANNEL_1_2 = 1,
		I2S_CHANNEL_3_4 = 3,
		I2S_CHANNEL_5_6 = 7,
		I2S_CHANNEL_7_8 = 0xf
	};
	#define v_I2S_CHANNEL(n)		((n) << 2)
	enum {
		I2S_STANDARD = 0,
		I2S_LEFT_JUSTIFIED,
		I2S_RIGHT_JUSTIFIED
	};
	#define v_I2S_MODE(n)				(n)

#define AUDIO_I2S_MAP				0x39
#define AUDIO_I2S_SWAPS_SPDIF		0x3a
	#define v_SPIDF_FREQ(n)			(n)	

#define N_32K 		0x1000
#define N_441K 		0x1880
#define N_882K 		0x3100
#define N_1764K 	0x6200
#define N_48K 		0x1800
#define N_96K		0x3000
#define N_192K 		0x6000

#define AUDIO_N_H					0x3f
#define AUDIO_N_M					0x40
#define AUDIO_N_L					0x41

#define AUDIO_CTS_H					0x45
#define AUDIO_CTS_M					0x46
#define AUDIO_CTS_L					0x47


#define DDC_CLK_L					0x4b
#define DDC_CLK_H					0x4c

#define EDID_SEGMENT_POINTER	0x4d
#define EDID_WORD_ADDR			0x4e
#define EDID_FIFO_OFFSET		0x4f
#define EDID_FIFO_ADDR			0x50

/* CONTROL_PACKET_BUF_INDEX */
#define CONTROL_PACKET_BUF_INDEX	0x9f
enum {
	INFOFRAME_AVI = 0x06,
	INFOFRAME_AAI = 0x08
};
#define CONTROL_PACKET_ADDR			0xa0


#define SIZE_AVI_INFOFRAME			0x11	// 14 bytes
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

#define HDCP_CTRL				0x52
	#define m_HDMI_DVI			(1 << 1)
	#define v_HDMI_DVI(n)		(n << 1)

#define INTERRUPT_MASK1			0xc0
#define INTERRUPT_STATUS1		0xc1
	#define m_INT_HOTPLUG		(1 << 7)
	#define	m_INT_ACTIVE_VSYNC	(1 << 6)
	#define m_INT_EDID_READY	(1 << 2)
	
#define INTERRUPT_MASK2			0xc2
#define INTERRUPT_STATUS2		0xc3
	#define m_INT_HDCP_ERR		(1 << 7)
	#define m_INT_BKSV_FLAG		(1 << 6)
	#define m_INT_HDCP_OK		(1 << 4)

#define HDMI_STATUS				0xc8
	#define m_HOTPLUG	(1 << 7)
	#define m_DDC_SDA	(1 << 5)
	#define m_DDC_SDC	(1 << 4)

#define PHY_SYNC				0xce		//sync phy parameter				

#define PHY_DRIVER				0xe1
	#define v_MAIN_DRIVER(n)	(n << 4)
	#define v_PRE_DRIVER(n)		(n << 2)
	#define v_TX_ENABLE(n)		(n << 1)
	
#define PHY_PRE_EMPHASIS		0xe2
	#define v_PRE_EMPHASIS(n)	(n << 4)
	#define v_TMDS_PWRDOWN(n)	(n)
	
#define PHY_PLL_TEST			0xe3
#define PHY_BANDGAP_PWR			0xe4
	#define v_BANDGAP_PWR_DOWN	0x03
	#define v_BANDGAP_PWR_UP	0
	
#define PHY_PLL_CTRL			0xe5
	#define v_PLL_DISABLE(n)	(n << 4)
	#define v_PLL_RESET(n)		(n << 3)
	#define v_TMDS_RESET(n)		(n << 2)
			
#define PHY_PLL_LDO_PWR			0xe7
	#define v_LDO_PWR_DOWN(n)	(n << 2)
		
#endif