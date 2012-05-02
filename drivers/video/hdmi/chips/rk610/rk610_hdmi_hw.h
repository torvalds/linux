#ifndef _RK610_HDMI_HW_H
#define _RK610_HDMI_HW_H
#include <linux/earlysuspend.h>

#define MAX_V_DESCRIPTORS				20
#define MAX_A_DESCRIPTORS				10
#define MAX_SPEAKER_CONFIGURATIONS	    4
#define AUDIO_DESCR_SIZE			 	3

#define EDID_BLOCK_SIZE         128
#define NUM_OF_EXTEN_ADDR       0x7e
#define EDID_HDR_NO_OF_FF   0x06

// Data Block Tag Codes
//====================================================
#define AUDIO_D_BLOCK       0x01
#define VIDEO_D_BLOCK       0x02
#define VENDOR_SPEC_D_BLOCK 0x03
#define SPKR_ALLOC_D_BLOCK  0x04
#define USE_EXTENDED_TAG    0x07
// Extended Data Block Tag Codes
//====================================================
#define COLORIMETRY_D_BLOCK 0x05

#define HDMI_SIGNATURE_LEN  0x03

#define CEC_PHYS_ADDR_LEN   0x02
#define EDID_EXTENSION_TAG  0x02
#define EDID_REV_THREE      0x03
#define EDID_DATA_START     0x04

#define EDID_BLOCK_0        0x00
#define EDID_BLOCK_2_3      0x01

#define VIDEO_CAPABILITY_D_BLOCK 0x00

//#define DEV_SUPPORT_CEC 
#if 1
#define MSBIT       	0x80
#define LSBIT          	0x01

#define TWO_LSBITS        	0x03
#define THREE_LSBITS   	0x07
#define FOUR_LSBITS    	0x0F
#define FIVE_LSBITS    	0x1F
#define SEVEN_LSBITS    	0x7F
#define TWO_MSBITS     	0xC0
#define EIGHT_BITS      	0xFF
#define BYTE_SIZE        	0x08
#define BITS_1_0          	0x03
#define BITS_2_1          	0x06
#define BITS_2_1_0        	0x07
#define BITS_3_2              	0x0C
#define BITS_4_3_2       	0x1C  
#define BITS_5_4              	0x30
#define BITS_5_4_3		0x38
#define BITS_6_5             	0x60
#define BITS_6_5_4        	0x70
#define BITS_7_6            	0xC0

#define TPI_INTERNAL_PAGE_REG		0xBC
#define TPI_INDEXED_OFFSET_REG	0xBD
#define TPI_INDEXED_VALUE_REG		0xBE

#define EDID_TAG_ADDR       0x00
#define EDID_REV_ADDR       0x01
#define EDID_TAG_IDX        0x02
#define LONG_DESCR_PTR_IDX  0x02
#define MISC_SUPPORT_IDX    0x03

#define ESTABLISHED_TIMING_INDEX        35      // Offset of Established Timing in EDID block
#define NUM_OF_STANDARD_TIMINGS          8
#define STANDARD_TIMING_OFFSET          38
#define LONG_DESCR_LEN                  18
#define NUM_OF_DETAILED_DESCRIPTORS      4

#define DETAILED_TIMING_OFFSET        0x36
#endif
enum{
    EDID_BLOCK0=0,
    EDID_BLOCK1,
    EDID_BLOCK2,
    EDID_BLOCK3,
};
#define RK610_SYS_FREG_CLK        11289600
#define RK610_SCL_RATE            (100*1000)
#define RK610_DDC_CONFIG          (RK610_SYS_FREG_CLK>>2)/RK610_SCL_RATE

#define FALSE               0
#define TRUE                1

//EVENT
#define RK610_HPD_EVENT  	1<<7
#define RK610_HPD_PLUG  	1<<7
#define RK610_EDID_EVENT 	1<<2

//output mode 0x52
#define DISPLAY_DVI         0
#define DISPLAY_HDMI        1

//0x00
#define RK610_INT_POL       1
#define RK610_SYS_PWR_ON    0
#define RK610_SYS_PWR_OFF   1
#define RK610_PHY_CLK       0
#define RK610_SYS_CLK       1

#define RK610_MCLK_FS       0x01    //256fs
//0x01
// INPUT_VIDEO_FORMAT
#define RGB_YUV444          0x00
#define DDR_RGB444_YUV444   0x05
#define DDR_YUV422          0x06

//0x02
//video output format
#define RGB444              0x00
#define YUV444              0x01
#define YUV422              0x02

//DATA WIDTH
#define DATA_12BIT          0X00
#define DATA_10BIT          0X01
#define DATA_8BIT           0X03

//0X04
//1:after 0:not After 1st sof for external DE sample
#define DE_AFTER_SOF        0
#define DE_NOAFTER_SOF      1

#define CSC_ENABLE          0
#define CSC_DISABLE         1

//0X05
#define CLEAR_AVMUTE(x)        (x)<<7
#define SET_AVMUTE(x)          (x)<<6
#define AUDIO_MUTE(x)          (x)<<1
#define VIDEO_BLACK(x)         (x)<<0    //1:black 0:normal

//0x08
#define VSYNC_POL(x)            (x)<<3   //0:Negative 1:Positive
#define HSYNC_POL(x)            (x)<<2      //0:Negative 1:Positive
#define INTER_PROGRESSIVE(x)    (x)<<1  //0: progressive 1:interlace
#define VIDEO_SET_ENABLE(x)     (x)<<0  //0:disable 1: enable

/*          0xe1        */  
//Main-driver strength :0000~1111: the strength from low to high
#define M_DRIVER_STR(x)         (((x)&0xf)<<4)
//Pre-driver strength  :00~11: the strength from low to high
#define P_DRIVER_STR(x)         (((x)&3)<<2)
//TX driver enable  1: enable   0: disable
#define TX_DRIVER_EN(x)         (((x)&1)<<1)
/*          0xe2        */ 
//Pre-emphasis strength 00~11: the strength from 0 to high
#define P_EMPHASIS_STR(x)       (((x)&3)<<4)
//Power down TMDS driver      1: power down. 0: not
#define PWR_DOWN_TMDS(x)        (((x)&1)<<0)
/*          0xe3        */ 
//PLL out enable.   Just for test. need set to 1¡¯b0
#define PLL_OUT_EN(x)           (((x)&1)<<7)
/*          0xe4        */
// Band-Gap power down  11: power down  00: not
#define BAND_PWR(x)             (((x)&3)<<0)
/*          0xe5        */ 
//PLL disable   1: disable  0: enable
#define PLL_PWR(x)              (((x)&1)<<4)
//  PLL reset   1: reset    0: not
#define PLL_RST(x)              (((x)&1)<<3)
//PHY TMDS channels reset   1: reset    0: not
#define TMDS_RST(x)             (((x)&1)<<2)
/*          0xe7        */ 
// PLL LDO power down   1: power down   0: not
#define PLL_LDO_PWR(x)      (((x)&1)<<2) 


/**********CONFIG CHANGE ************/
#define VIDEO_CHANGE            1<<0
#define AUDIO_CHANGE            1<<1

#define byte    u8

#define HDMI_VIC_1080p_50Hz	    0x1f
#define HDMI_VIC_1080p_60Hz 	0x10
#define HDMI_VIC_720p_50Hz 	    0x13
#define HDMI_VIC_720p_60Hz		0x04
#define HDMI_VIC_576p_50Hz	    0x11
#define HDMI_VIC_480p_60Hz  	0x02

struct edid_result{
    bool supported_720p_50Hz;
	bool supported_720p_60Hz;
	bool supported_576p_50Hz;
	bool supported_720x480p_60Hz;
	bool supported_1080p_50Hz;
	bool supported_1080p_60Hz;
};
typedef struct edid_info
{												// for storing EDID parsed data
	byte edidDataValid;
	byte VideoDescriptor[MAX_V_DESCRIPTORS];	// maximum number of video descriptors
	byte AudioDescriptor[MAX_A_DESCRIPTORS][3];	// maximum number of audio descriptors
	byte SpkrAlloc[MAX_SPEAKER_CONFIGURATIONS];	// maximum number of speaker configurations
	byte UnderScan;								// "1" if DTV monitor underscans IT video formats by default
	byte BasicAudio;								// Sink supports Basic Audio
	byte YCbCr_4_4_4;							// Sink supports YCbCr 4:4:4
	byte YCbCr_4_2_2;							// Sink supports YCbCr 4:2:2
	byte HDMI_Sink;								// "1" if HDMI signature found
	byte CEC_A_B;								// CEC Physical address. See HDMI 1.3 Table 8-6
	byte CEC_C_D;
	byte ColorimetrySupportFlags;				// IEC 61966-2-4 colorimetry support: 1 - xvYCC601; 2 - xvYCC709 
	byte MetadataProfile;
	byte _3D_Supported;
	
} EDID_INF;
enum EDID_ErrorCodes
{
	EDID_OK,
	EDID_INCORRECT_HEADER,
	EDID_CHECKSUM_ERROR,
	EDID_NO_861_EXTENSIONS,
	EDID_SHORT_DESCRIPTORS_OK,
	EDID_LONG_DESCRIPTORS_OK,
	EDID_EXT_TAG_ERROR,
	EDID_REV_ADDR_ERROR,
	EDID_V_DESCR_OVERFLOW,
	EDID_UNKNOWN_TAG_CODE,
	EDID_NO_DETAILED_DESCRIPTORS,
	EDID_DDC_BUS_REQ_FAILURE,
	EDID_DDC_BUS_RELEASE_FAILURE
};
enum PWR_MODE{
    NORMAL,
    LOWER_PWR,
};
struct rk610_hdmi_hw_inf{
    struct i2c_client *client;
    EDID_INF *edid_inf;
    u8 video_format;
    u8 audio_fs;
    u8 config_param;
    bool suspend_flag;
    bool hpd;
	bool analog_sync;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
extern int Rk610_hdmi_suspend(struct i2c_client *client);
extern int Rk610_hdmi_resume(struct i2c_client *client);
#endif
extern void Rk610_hdmi_plug(struct i2c_client *client);
extern void Rk610_hdmi_unplug(struct i2c_client *client);
extern int Rk610_hdmi_Set_Video(u8 video_format);
extern int Rk610_hdmi_Set_Audio(u8 audio_fs);
extern int Rk610_hdmi_Config_Done(struct i2c_client *client);
extern int Rk610_Get_Optimal_resolution(int resolution_set);
extern void Rk610_hdmi_event_work(struct i2c_client *client, bool *hpd);
extern int Rk610_hdmi_init(struct i2c_client *client);
#endif
